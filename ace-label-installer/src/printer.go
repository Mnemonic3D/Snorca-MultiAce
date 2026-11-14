package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"syscall"
	"time"

	"golang.org/x/crypto/ssh"
)

// ---------- address ----------

// parseAddress accepts only a literal IP address, so the program never asks a name server anything:
// the only machine it talks to is the printer at this address.
func parseAddress(s string) (string, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return "", errors.New("Type the printer's IP address first.")
	}
	ip := net.ParseIP(strings.Trim(s, "[]"))
	if ip == nil {
		return "", fmt.Errorf("%q is not an IP address. It looks like four numbers with dots, for example 192.0.2.50.", s)
	}
	if ip.IsUnspecified() || ip.IsMulticast() || ip.Equal(net.IPv4bcast) {
		return "", fmt.Errorf("%s cannot be a printer's address.", s)
	}
	return ip.String(), nil
}

// ---------- Moonraker (no password needed) ----------

type probeResult struct {
	Answered   bool   // Moonraker answered on port 7125 or 80
	Port       string // the port that answered
	IsU1       int    // 1 yes, 0 cannot tell
	PrintState string // standby, printing, ... or "" when unreadable
	Detail     string
}

func httpClient() *http.Client {
	return &http.Client{
		Timeout: 6 * time.Second,
		Transport: &http.Transport{
			Proxy:             nil, // never use a proxy from the environment: talk to the printer only
			DialContext:       (&net.Dialer{Timeout: 4 * time.Second}).DialContext,
			DisableKeepAlives: true,
		},
		CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse },
	}
}

func getJSON(c *http.Client, url string, v any) error {
	resp, err := c.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	return json.NewDecoder(io.LimitReader(resp.Body, 1<<20)).Decode(v)
}

func probeMoonraker(ip string, logf func(string, ...any)) probeResult {
	c := httpClient()
	var r probeResult
	for _, port := range []string{"7125", "80"} {
		base := "http://" + net.JoinHostPort(ip, port)
		var info struct {
			Result struct {
				State    string `json:"state"`
				Hostname string `json:"hostname"`
			} `json:"result"`
		}
		if err := getJSON(c, base+"/printer/info", &info); err != nil {
			logf("Moonraker on port %s: no answer (%v)", port, shortErr(err))
			continue
		}
		r.Answered, r.Port = true, port
		logf("Moonraker on port %s answered, Klipper state %q", port, info.Result.State)
		var list struct {
			Result struct {
				Objects []string `json:"objects"`
			} `json:"result"`
		}
		if err := getJSON(c, base+"/printer/objects/list", &list); err == nil {
			for _, o := range list.Result.Objects {
				// Snapmaker U1 specific Klipper objects (from the U1 printer.cfg)
				if o == "print_task_config" || o == "fm175xx_reader" {
					r.IsU1 = 1
				}
			}
			logf("Klipper objects: %d, U1 objects seen: %v", len(list.Result.Objects), r.IsU1 == 1)
		}
		var q struct {
			Result struct {
				Status struct {
					PrintStats struct {
						State string `json:"state"`
					} `json:"print_stats"`
				} `json:"status"`
			} `json:"result"`
		}
		if err := getJSON(c, base+"/printer/objects/query?print_stats", &q); err == nil {
			r.PrintState = q.Result.Status.PrintStats.State
		}
		logf("Print state: %q", r.PrintState)
		return r
	}
	return r
}

func shortErr(err error) string {
	s := err.Error()
	if len(s) > 160 {
		s = s[:160] + "..."
	}
	return s
}

// ---------- SSH ----------

// askFunc shows a question in the window and waits for the answer.
type askFunc func(msg, yes, no string) bool

type printer struct {
	mu       sync.Mutex
	ip       string
	client   *ssh.Client
	accepted map[string]string // address -> accepted host key fingerprint, for this run only (never written)
	logf     func(string, ...any)
}

func newPrinter(logf func(string, ...any)) *printer {
	return &printer{accepted: map[string]string{}, logf: logf}
}

func (p *printer) close() {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.client != nil {
		p.client.Close()
		p.client = nil
	}
}

// errKind sorts connection failures so the window can say something plain.
type connError struct {
	kind string // refused, unreachable, key, auth, closed
	err  error
}

func (e *connError) Error() string { return e.kind + ": " + e.err.Error() }

func classifyDial(err error) *connError {
	var ne net.Error
	switch {
	case errors.Is(err, syscall.ECONNREFUSED):
		return &connError{"refused", err}
	case errors.As(err, &ne) && ne.Timeout():
		return &connError{"unreachable", err}
	default:
		return &connError{"unreachable", err}
	}
}

// sshAlive reports whether an SSH server answers on ip:22 with its greeting ("SSH-..."). A bare TCP connect
// is not enough: some network paths accept the connection even while the printer itself is down.
func sshAlive(ip string, timeout time.Duration) bool {
	c, err := net.DialTimeout("tcp", net.JoinHostPort(ip, "22"), timeout)
	if err != nil {
		return false
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(timeout + 2*time.Second))
	buf := make([]byte, 4)
	if _, err := io.ReadFull(c, buf); err != nil {
		return false
	}
	return string(buf) == "SSH-"
}

// connect signs in as root with the password. The password is passed only to the SSH library in memory.
func (p *printer) connect(ip, password string, ask askFunc, afterRestart bool) error {
	p.close()
	addr := net.JoinHostPort(ip, "22")
	conn, err := net.DialTimeout("tcp", addr, 8*time.Second)
	if err != nil {
		return classifyDial(err)
	}
	conn.SetDeadline(time.Now().Add(30 * time.Second))
	keyRefused := false
	cfg := &ssh.ClientConfig{
		User: "root",
		Auth: []ssh.AuthMethod{
			ssh.Password(password),
			ssh.KeyboardInteractive(func(name, instr string, qs []string, echos []bool) ([]string, error) {
				ans := make([]string, len(qs))
				for i, q := range qs {
					if strings.Contains(strings.ToLower(q), "password") {
						ans[i] = password
					}
				}
				return ans, nil
			}),
		},
		HostKeyCallback: func(hostname string, remote net.Addr, key ssh.PublicKey) error {
			fp := ssh.FingerprintSHA256(key)
			p.logf("Printer key: %s %s", key.Type(), fp)
			old, seen := p.accepted[ip]
			if seen && old == fp {
				return nil
			}
			var msg string
			if seen {
				msg = "The printer at " + ip + " shows a different key than before.\n" +
					"Old: " + old + "\nNew: " + fp + "\n\n"
				if afterRestart {
					msg += "This printer can make a new key when it restarts. Accept only if this is your printer and nobody else could have put a different device on this address."
				} else {
					msg += "Accept only if you are sure this is your printer."
				}
			} else {
				msg = "First connection to the printer at " + ip + ".\nIts key fingerprint is:\n" + fp +
					"\n\nAccept it only if this is your printer. It is remembered only until you close this window."
			}
			conn.SetDeadline(time.Time{}) // the person may take a while to answer
			ok := ask(msg, "Accept", "Cancel")
			conn.SetDeadline(time.Now().Add(30 * time.Second))
			if !ok {
				keyRefused = true
				return errors.New("key not accepted")
			}
			p.accepted[ip] = fp
			return nil
		},
		Timeout: 15 * time.Second,
	}
	sc, chans, reqs, err := ssh.NewClientConn(conn, addr, cfg)
	if err != nil {
		conn.Close()
		switch {
		case keyRefused:
			return &connError{"key", err}
		case strings.Contains(err.Error(), "unable to authenticate"):
			return &connError{"auth", err}
		default:
			return &connError{"closed", err}
		}
	}
	conn.SetDeadline(time.Time{})
	p.mu.Lock()
	p.client = ssh.NewClient(sc, chans, reqs)
	p.ip = ip
	p.mu.Unlock()
	p.logf("Signed in to %s as root", ip)
	return nil
}

var errNotConnected = errors.New("not signed in")

// run runs one fixed shell command on the printer. The commands are constants in this file; nothing typed by
// the person (not the address, never the password) is ever put into a command.
func (p *printer) run(cmd string, stdin []byte, timeout time.Duration) (string, int, error) {
	p.mu.Lock()
	c := p.client
	p.mu.Unlock()
	if c == nil {
		return "", -1, errNotConnected
	}
	s, err := c.NewSession()
	if err != nil {
		return "", -1, err
	}
	defer s.Close()
	var out, errb bytes.Buffer
	s.Stdout, s.Stderr = &out, &errb
	if stdin != nil {
		s.Stdin = bytes.NewReader(stdin)
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- s.Run(cmd) }()
	select {
	case err = <-done:
	case <-ctx.Done():
		s.Close()
		return out.String(), -1, errors.New("the printer did not answer in time")
	}
	if e := strings.TrimSpace(errb.String()); e != "" {
		p.logf("printer said: %s", oneLine(e))
	}
	if err == nil {
		return out.String(), 0, nil
	}
	var ee *ssh.ExitError
	if errors.As(err, &ee) {
		return out.String(), ee.ExitStatus(), nil
	}
	return out.String(), -1, err
}

func oneLine(s string) string {
	s = strings.ReplaceAll(s, "\n", " | ")
	if len(s) > 300 {
		s = s[:300] + "..."
	}
	return s
}

// ---------- the fixed commands ----------

// The print-state query is the same one install.sh runs on the printer.
const printStateCmd = `python3 -c 'import json, urllib.request
try:
    d = json.load(urllib.request.urlopen("http://127.0.0.1:7125/printer/objects/query?print_stats", timeout=5))
    print(d["result"]["status"]["print_stats"]["state"])
except Exception:
    pass' 2>/dev/null`

const checkCmd = `echo "uid=$(id -u)"
if [ -e /usr/bin/gui ]; then echo gui=yes; else echo gui=no; fi
echo "gui_sha=$(sha256sum /usr/bin/gui 2>/dev/null | cut -d' ' -f1)"
if [ -d /etc/hooks/lmd.d ]; then echo hooks=yes; else echo hooks=no; fi
echo "lib_sha=$(sha256sum /usr/local/lib/libace-label.so 2>/dev/null | cut -d' ' -f1)"
echo "hook_sha=$(sha256sum /etc/hooks/lmd.d/40-ace-label.sh 2>/dev/null | cut -d' ' -f1)"
if [ -e /usr/local/lib/libace-label.so ] || [ -e /etc/hooks/lmd.d/40-ace-label.sh ]; then echo label_any=yes; else echo label_any=no; fi
if [ -e /oem/.debug ]; then echo debug=yes; else echo debug=no; fi
if [ -e /oem/.ace-label-made-debug ]; then echo note=yes; else echo note=no; fi
echo "state=$(` + printStateCmd + `)"
`

// Unlock: switch on "keep system changes across reboots" and remember that we did it.
const unlockCmd = `if [ -e /oem/.debug ]; then echo already; exit 0; fi
touch /oem/.debug || exit 21
touch /oem/.ace-label-made-debug || { rm -f /oem/.debug; exit 22; }
sync
echo made`

const rebootCmd = `sync; reboot`

const copyLibCmd = `mkdir -p /usr/local/lib || exit 31
cat > /usr/local/lib/libace-label.so || exit 32
chmod 644 /usr/local/lib/libace-label.so || exit 32`

const copyHookCmd = `cat > /etc/hooks/lmd.d/40-ace-label.sh || exit 33
chmod 644 /etc/hooks/lmd.d/40-ace-label.sh || exit 33`

const verifyCmd = `echo "lib_sha=$(sha256sum /usr/local/lib/libace-label.so 2>/dev/null | cut -d' ' -f1)"
echo "hook_sha=$(sha256sum /etc/hooks/lmd.d/40-ace-label.sh 2>/dev/null | cut -d' ' -f1)"
echo "lib_mode=$(stat -c %a /usr/local/lib/libace-label.so 2>/dev/null)"
echo "hook_mode=$(stat -c %a /etc/hooks/lmd.d/40-ace-label.sh 2>/dev/null)"
sync`

const rollbackLibCmd = `rm -f /usr/local/lib/libace-label.so; [ ! -e /usr/local/lib/libace-label.so ]`
const rollbackBothCmd = `rm -f /usr/local/lib/libace-label.so /etc/hooks/lmd.d/40-ace-label.sh; [ ! -e /usr/local/lib/libace-label.so ] && [ ! -e /etc/hooks/lmd.d/40-ace-label.sh ]`

const removeCmd = `rm -f /usr/local/lib/libace-label.so /etc/hooks/lmd.d/40-ace-label.sh
[ ! -e /usr/local/lib/libace-label.so ] && [ ! -e /etc/hooks/lmd.d/40-ace-label.sh ] && sync`

const switchOffCmd = `rm -f /oem/.debug /oem/.ace-label-made-debug
[ ! -e /oem/.debug ] && [ ! -e /oem/.ace-label-made-debug ] && sync`

// ---------- check results ----------

type checks struct {
	uid, gui, guiSHA, hooks, libSHA, hookSHA, labelAny, debug, note, state string
}

func parseKV(out string) map[string]string {
	m := map[string]string{}
	sc := bufio.NewScanner(strings.NewReader(out))
	for sc.Scan() {
		k, v, ok := strings.Cut(sc.Text(), "=")
		if ok {
			m[strings.TrimSpace(k)] = strings.TrimSpace(v)
		}
	}
	return m
}

func (p *printer) check() (checks, error) {
	out, code, err := p.run(checkCmd, nil, 40*time.Second)
	if err != nil {
		return checks{}, err
	}
	if code != 0 {
		return checks{}, fmt.Errorf("check exited with %d", code)
	}
	m := parseKV(out)
	c := checks{m["uid"], m["gui"], m["gui_sha"], m["hooks"], m["lib_sha"], m["hook_sha"], m["label_any"], m["debug"], m["note"], m["state"]}
	p.logf("Checks: uid=%s gui=%s gui_sha=%s hooks=%s label_installed=%v label_any=%s debug=%s note=%s state=%q",
		c.uid, c.gui, short(c.guiSHA), c.hooks, c.installed(), c.labelAny, c.debug, c.note, c.state)
	if c.uid == "" {
		return c, errors.New("the printer's answer was not understood")
	}
	return c, nil
}

func short(s string) string {
	if len(s) > 12 {
		return s[:8] + "..." + s[len(s)-8:]
	}
	return s
}

func (c checks) installed() bool { return c.libSHA == libSHA && c.hookSHA == hookSHA }

// gate applies install.sh's safety stops in install.sh's order. It returns "" when all pass, or the
// reason to stop. idleUnknown is true when the print state could not be read (install.sh then asks).
func (c checks) gate() (stop string, idleUnknown bool) {
	if ok, why := payloadOK(); !ok {
		return why, false
	}
	if c.uid != "0" {
		return "the sign-in is not root.", false
	}
	if c.gui != "yes" {
		return "this does not look like a Snapmaker U1 (no /usr/bin/gui).", false
	}
	if c.guiSHA != guiSHA {
		return "this printer's screen program is a different version. The label only works on PAXX extended firmware 2.0.0 build 24.", false
	}
	if c.hooks != "yes" {
		return "this firmware has no /etc/hooks/lmd.d folder. PAXX extended firmware 2.0.0 build 24 is needed.", false
	}
	switch c.state {
	case "standby", "complete", "cancelled", "error":
		return "", false
	case "printing", "paused":
		return "the printer is " + c.state + ". Wait until it is idle.", false
	}
	return "", true
}
