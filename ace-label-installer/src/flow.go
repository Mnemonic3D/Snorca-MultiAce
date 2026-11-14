package main

import (
	"errors"
	"fmt"
	"strings"
	"time"
)

type app struct {
	u           *ui
	p           *printer
	ip          string // the address checked in step 1 (and changed while waiting after the unlock)
	waitSeconds int
	signedIn    bool
	last        checks
	unlockedNow bool // we switched on keep-system-changes in this run
}

const nothingMore = " Nothing more was changed."

// work runs fn in the background with the buttons locked, so the window stays responsive.
func (a *app) work(fn func()) {
	if a.u.busy {
		return
	}
	a.u.busy = true
	go func() {
		a.u.setBusy(true)
		defer a.u.setBusy(false)
		fn()
	}()
}

// ---------- step 1 ----------

func (a *app) checkPrinter() {
	raw := a.u.ipIn.Value()
	a.work(func() {
		u := a.u
		u.gui(func() {
			for _, l := range u.s1lines {
				l.set(iconNone, "")
			}
			u.s1msg.hide()
			u.s1ok.hide()
			u.next1.Deactivate()
			a.ip = "" // only a printer that passes this check can be used in step 2
		})
		ip, err := parseAddress(raw)
		if err != nil {
			u.gui(func() { u.s1lines[0].set(iconBad, "Not an IP address"); u.s1msg.show(toneStop, err.Error()) })
			return
		}
		u.logf("Step 1: checking %s (no sign-in, read-only)", ip)
		u.gui(func() { u.s1lines[0].set(iconNone, "Asking the printer at "+ip+"...") })
		r := probeMoonraker(ip, u.logf)
		ok := true
		u.gui(func() {
			if !r.Answered {
				u.s1lines[0].set(iconBad, "No printer answered at "+ip)
				u.s1msg.show(toneStop, "Nothing answered at this address. Check the IP address on the touch screen (Settings, Network) and that this computer is on the same network as the printer. Nothing was changed.")
				ok = false
				return
			}
			u.s1lines[0].set(iconNone, "")
			found := "Snapmaker U1 found."
			if r.IsU1 != 1 {
				found = "A printer answered at this address."
			}
			idle := "Printer is idle."
			switch r.PrintState {
			case "standby", "complete", "cancelled", "error":
			case "printing", "paused":
				u.s1lines[0].set(iconBad, "Printer is "+r.PrintState)
				u.s1msg.show(toneStop, "Stopped: the printer is "+r.PrintState+". Wait until it is idle, then press Check printer again. Nothing was changed.")
				ok = false
			default:
				idle = "Its print state could not be read here; the installer asks again after you sign in."
			}
			if ok {
				u.s1ok.show(toneOK, found+"\n"+idle+"\nClick Next for the next step.")
				a.ip = ip
				u.next1.Activate()
			}
		})
	})
}

func (a *app) toStep2() {
	if a.u.busy || a.ip == "" {
		return
	}
	a.signedIn = false
	a.resetStep2()
	a.u.show(2)
	a.u.pwIn.TakeFocus()
}

func (a *app) resetStep2() {
	u := a.u
	u.s2intro.SetLabel(s2introBefore)
	for _, l := range u.s2lines {
		l.set(iconNone, "")
	}
	u.s2panel.hide()
	u.unlockB.Hide()
	u.s2wait.SetLabel("")
	u.s2ipLabel.Hide()
	u.s2ipIn.Hide()
	u.signB.Show()
	u.next2.Deactivate()
}

func (a *app) backToStep1() {
	if a.u.busy {
		return
	}
	a.p.close()
	a.signedIn = false
	if a.ip != "" {
		a.u.next1.Activate() // the printer was already checked; Next works again without a new check
	}
	a.u.show(1)
}

func (a *app) next2() {
	if a.u.busy {
		return
	}
	if !a.signedIn {
		a.signIn()
		return
	}
	if a.last.debug == "yes" {
		a.u.show(3)
	}
}

// ---------- step 2: sign in and check ----------

func (a *app) signIn() {
	pw := a.u.pwIn.Value()
	a.work(func() {
		u := a.u
		if pw == "" {
			u.gui(func() { u.s2panel.show(toneStop, "Type the printer's root password first.") })
			return
		}
		u.gui(func() { a.resetStep2(); u.s2lines[0].set(iconDot, "Signing in to "+a.ip+"...") })
		u.logf("Step 2: signing in to %s", a.ip)
		if err := a.p.connect(a.ip, pw, u.ask, false); err != nil {
			a.showConnError(err)
			return
		}
		a.signedIn = true
		a.afterSignIn(false)
	})
}

func (a *app) showConnError(err error) {
	u := a.u
	u.logf("Sign-in failed: %v", err)
	var ce *connError
	msg := "Stopped: could not sign in to the printer (" + shortErr(err) + ")." + nothingMore
	if errors.As(err, &ce) {
		switch ce.kind {
		case "refused":
			msg = "Stopped: the printer at " + a.ip + " did not accept a sign-in connection (SSH, port 22). SSH may be switched off in the PAXX extended firmware settings." + nothingMore
		case "unreachable":
			msg = "Stopped: could not reach the printer at " + a.ip + ". Check that it is switched on and on the same network." + nothingMore
		case "key":
			msg = "Stopped: you did not accept the printer's key." + nothingMore
		case "auth":
			msg = "The password was not accepted. Check it and press Sign in again." + nothingMore
		case "closed":
			msg = "Stopped: the printer closed the connection during sign-in (" + shortErr(ce.err) + ")." + nothingMore
		}
	}
	u.gui(func() {
		u.s2lines[0].set(iconBad, "Not signed in")
		u.s2panel.show(toneStop, msg)
	})
}

// gateNow runs the checks and install.sh's safety stops. It returns false (after showing why) when the
// installer must stop.
func (a *app) gateNow(where *panel, lines []*statusLine) (checks, bool) {
	u := a.u
	c, err := a.p.check()
	if err != nil {
		u.gui(func() { where.show(toneStop, "Stopped: could not read the printer's state ("+shortErr(err)+")."+nothingMore) })
		return c, false
	}
	stop, idleUnknown := c.gate()
	if stop == "" && idleUnknown {
		if !u.ask("Could not read the printer's state.\n\nIs the printer idle (not printing)?", "Yes, it is idle", "No") {
			stop = "not confirmed idle."
		}
	}
	u.gui(func() {
		if lines != nil {
			fw := iconOK
			fwText := "Firmware: PAXX 2.0.0 build 24"
			if c.gui != "yes" || c.guiSHA != guiSHA || c.hooks != "yes" {
				fw, fwText = iconBad, "Firmware: not PAXX 2.0.0 build 24"
			}
			lines[0].set(iconOK, "Signed in. Snapmaker U1 screen program found")
			if c.gui != "yes" {
				lines[0].set(iconBad, "Signed in, but no U1 screen program found")
			}
			lines[1].set(fw, fwText)
			switch c.state {
			case "printing", "paused":
				lines[2].set(iconBad, "Printer is "+c.state)
			case "standby", "complete", "cancelled", "error":
				lines[2].set(iconOK, "Printer is idle")
			default:
				lines[2].set(iconDot, "Print state unknown")
			}
			if c.installed() {
				lines[3].set(iconOK, "ACE label: already installed")
			} else if c.labelAny == "yes" {
				lines[3].set(iconDot, "ACE label: a different or broken copy is there")
			} else {
				lines[3].set(iconDot, "ACE label: not installed yet")
			}
			if c.debug == "yes" {
				lines[4].set(iconOK, "Keeps system changes across reboots")
			} else {
				lines[4].set(iconDot, "Does not keep system changes yet")
			}
		}
		if stop != "" {
			where.show(toneStop, "Stopped: "+stop+nothingMore)
		}
	})
	a.last = c
	return c, stop == ""
}

func (a *app) afterSignIn(afterUnlock bool) {
	u := a.u
	lines := u.s2lines[:]
	c, ok := a.gateNow(u.s2panel, lines)
	if !ok {
		return
	}
	if c.debug == "yes" {
		if afterUnlock {
			u.logf("/oem/.debug is active after the reboot: going to step 3")
		} else {
			u.logf("/oem/.debug is already present: skipping the unlock, going to step 3")
		}
		u.gui(func() {
			a.prepareStep3(afterUnlock)
			u.next2.Activate()
			u.show(3)
		})
		return
	}
	if afterUnlock {
		u.gui(func() {
			u.s2panel.show(toneStop, "Stopped: the printer came back, but it still does not keep system changes (/oem/.debug is missing). The label would be wiped at the next reboot, so it was not installed."+nothingMore)
		})
		return
	}
	u.gui(func() {
		u.s2intro.SetLabel(s2introLocked)
		u.s2panel.show(toneCaution, cautionText)
		u.unlockB.Show()
		u.win.Redraw()
	})
}

// ---------- step 2: unlock, reboot, wait ----------

func (a *app) unlock() {
	pw := a.u.pwIn.Value()
	a.work(func() {
		u := a.u
		if !u.ask("Unlock the printer and reboot it now?\n\nAfter the reboot, enter your Wi-Fi password again on the printer's touch screen. Have your Wi-Fi name and password ready.", "Unlock and reboot", "Cancel") {
			u.logf("Unlock cancelled. Nothing was changed.")
			return
		}
		// The safety stops again, right before changing anything.
		c, ok := a.gateNow(u.s2panel, u.s2lines[:])
		if !ok {
			return
		}
		if c.debug == "yes" {
			u.gui(func() { a.prepareStep3(false); u.show(3) })
			return
		}
		out, code, err := a.p.run(unlockCmd, nil, 60*time.Second)
		if err != nil || (code != 0) {
			msg := "Stopped: could not switch on keep-system-changes (/oem/.debug)."
			switch {
			case code == 21:
				msg = "Stopped: could not create /oem/.debug." + nothingMore
			case code == 22:
				msg = "Stopped: could not create the installer's note file, so /oem/.debug was taken out again." + nothingMore
			default:
				msg += " The printer's answer was lost (" + errText(err) + "). Sign in again to see what is set." + nothingMore
			}
			u.logf("Unlock failed: code %d %v", code, err)
			u.gui(func() { u.s2panel.show(toneStop, msg) })
			return
		}
		if strings.TrimSpace(out) == "made" {
			a.unlockedNow = true
			u.logf("Created /oem/.debug and the note file /oem/.ace-label-made-debug")
		}
		u.logf("Rebooting the printer")
		_, rcode, rerr := a.p.run(rebootCmd, nil, 60*time.Second)
		u.logf("Reboot command: code %d, %v (a lost connection is expected here)", rcode, rerr)
		a.p.close()
		if rerr == nil && rcode != 0 {
			u.gui(func() {
				u.s2panel.show(toneStop, "The printer is unlocked, but it did not accept the reboot command. Switch it off and on with its power switch, then start this installer again: it will go straight to step 3.")
			})
			return
		}
		a.waitForPrinter(pw)
	})
}

func errText(err error) string {
	if err == nil {
		return "no answer"
	}
	return shortErr(err)
}

// waitForPrinter waits until the printer has gone down and come back, signs in again and checks it.
// The address can be changed in the window while waiting (the printer may get a new address after the Wi-Fi
// is entered again).
func (a *app) waitForPrinter(pw string) {
	u := a.u
	orig := a.ip
	u.gui(func() {
		u.s2panel.show(toneCaution, "The printer is rebooting.\nENTER YOUR WI-FI PASSWORD AGAIN on the printer's touch screen when it asks.\n\nIf the printer shows a new IP address, type it below.")
		u.unlockB.Hide()
		u.s2wait.SetLabel("Waiting for the printer to come back...\nReconnect its Wi-Fi on the touch screen.")
		u.s2ipIn.SetValue(orig)
		u.s2ipLabel.Show()
		u.s2ipIn.Show()
		u.s2ipIn.Activate()
		u.win.Redraw()
	})
	start := time.Now()
	downSeen := false
	warned := false
	limit := time.Duration(a.waitSeconds) * time.Second
	for {
		var cur string
		u.gui(func() { cur = u.s2ipIn.Value() })
		ip, perr := parseAddress(cur)
		elapsed := time.Since(start).Round(time.Second)
		if perr == nil && ip != orig {
			downSeen = true // a new address: the old one no longer matters
		}
		if !downSeen {
			if !sshAlive(orig, 2*time.Second) {
				downSeen = true
				u.logf("The printer went down after %v", elapsed)
			} else if elapsed > 120*time.Second && !warned {
				warned = true
				u.gui(func() {
					u.s2wait.SetLabel("The printer has not restarted yet. If it does not, switch it off\nand on with its power switch. Still waiting...")
				})
			}
		} else if perr == nil && sshAlive(ip, 2*time.Second) {
			u.logf("The printer answers again at %s after %v", ip, elapsed)
			time.Sleep(3 * time.Second)
			err := a.p.connect(ip, pw, u.ask, true)
			if err == nil {
				a.ip = ip
				a.signedIn = true
				u.gui(func() {
					u.s2wait.SetLabel("The printer is back.")
					u.s2ipLabel.Hide()
					u.s2ipIn.Hide()
					u.s2panel.hide()
				})
				a.afterSignIn(true)
				return
			}
			var ce *connError
			if errors.As(err, &ce) && (ce.kind == "key" || ce.kind == "auth") {
				a.ip = ip
				a.showConnError(err)
				u.gui(func() {
					u.s2wait.SetLabel("The printer is unlocked. To go on, sign in again: it will go straight to step 3.")
					u.s2ipLabel.Hide()
					u.s2ipIn.Hide()
				})
				return
			}
			u.logf("Not ready yet: %v", err)
		}
		if elapsed > limit {
			span := fmt.Sprintf("%d minutes", a.waitSeconds/60)
			if a.waitSeconds%60 != 0 {
				span = fmt.Sprintf("%d seconds", a.waitSeconds)
			}
			if !u.ask("The printer did not come back within "+span+".\n\nIf its address changed, type the new address in the box first.\n\nKeep waiting?", "Keep waiting", "Stop") {
				u.gui(func() {
					u.s2ipLabel.Hide()
					u.s2ipIn.Hide()
					u.s2wait.SetLabel("")
					u.s2panel.show(toneStop, "Stopped waiting. The printer was unlocked before the reboot (keep system changes is on). When it is back on the network, start this installer again: it will go straight to step 3."+nothingMore)
				})
				return
			}
			start = time.Now()
		}
		time.Sleep(1500 * time.Millisecond)
	}
}

// ---------- step 3 ----------

func (a *app) prepareStep3(afterUnlock bool) {
	u := a.u
	c := a.last
	state := "Checked: PAXX 2.0.0 build 24, printer idle, keeps system changes."
	if !afterUnlock && !a.unlockedNow {
		state = "Checked: PAXX 2.0.0 build 24, printer idle. Your printer already keeps system changes, so step 2 was skipped."
	}
	u.s3state.SetLabel(state)
	u.s3panel.hide()
	u.installB.SetLabel("Install")
	if c.installed() {
		u.installB.SetLabel("Install again")
		u.s3panel.show(toneInfo, "The ACE label is already installed on this printer. Files checked.\n\"Install again\" copies it again; \"Remove the label\" takes it out.")
	}
	u.installB.Activate()
	u.removeB.Activate()
}

func (a *app) install() {
	a.work(func() {
		u := a.u
		u.gui(func() { u.s3panel.show(toneInfo, "Checking the printer again...") })
		u.logf("Step 3: install")
		if ok, why := payloadOK(); !ok {
			u.gui(func() { u.s3panel.show(toneStop, "Stopped: "+why+nothingMore) })
			return
		}
		c, ok := a.gateNow(u.s3panel, nil)
		if !ok {
			return
		}
		if c.debug != "yes" {
			u.gui(func() {
				u.s3panel.show(toneStop, "Stopped: the printer does not keep system changes (/oem/.debug is missing), so the label would be wiped at the next reboot. Go back and unlock it first."+nothingMore)
			})
			return
		}
		u.gui(func() { u.s3panel.show(toneInfo, "Copying the label file...") })
		_, code, err := a.p.run(copyLibCmd, libFile, 90*time.Second)
		if err != nil || code != 0 {
			u.logf("Copy of libace-label.so failed: code %d %v", code, err)
			a.rollback(rollbackLibCmd, "could not copy libace-label.so.", "The part that was copied was taken out again.")
			return
		}
		u.logf("Copied libace-label.so (%d bytes)", len(libFile))
		u.gui(func() { u.s3panel.show(toneInfo, "Adding the start-up hook...") })
		_, code, err = a.p.run(copyHookCmd, hookFile, 60*time.Second)
		if err != nil || code != 0 {
			u.logf("Copy of 40-ace-label.sh failed: code %d %v", code, err)
			a.rollback(rollbackBothCmd, "could not copy 40-ace-label.sh.", "The label file was taken out again.")
			return
		}
		u.logf("Copied 40-ace-label.sh (%d bytes)", len(hookFile))
		u.gui(func() { u.s3panel.show(toneInfo, "Checking both files...") })
		out, code, err := a.p.run(verifyCmd, nil, 90*time.Second)
		m := parseKV(out)
		u.logf("After copy: lib_sha=%s hook_sha=%s modes %s/%s", short(m["lib_sha"]), short(m["hook_sha"]), m["lib_mode"], m["hook_mode"])
		if err != nil || code != 0 || m["lib_sha"] != libSHA || m["hook_sha"] != hookSHA {
			a.rollback(rollbackBothCmd, "the copied files do not match.", "They were taken out again.")
			return
		}
		u.logf("Installed. Files checked.")
		u.gui(func() {
			for _, b := range u.s3bullets[:3] {
				b.set(iconOK, b.txt.Label())
			}
			u.installB.SetLabel("Install again")
			u.s3panel.show(toneOK, "Installed. Files checked.\nAfter the reboot the touch screen shows\nACE under the toolhead that has an ACE.")
		})
		a.askReboot("The label appears after the next reboot.")
	})
}

// rollback takes out what was copied. It never claims "nothing more was changed" unless the rollback worked.
func (a *app) rollback(cmd, what, undone string) {
	u := a.u
	_, code, err := a.p.run(cmd, nil, 30*time.Second)
	if err != nil {
		// The connection may have dropped: sign in once more just to clean up.
		u.logf("Rollback over the old connection failed (%v); signing in again to clean up", err)
		pw := ""
		u.gui(func() { pw = u.pwIn.Value() })
		if cerr := a.p.connect(a.ip, pw, u.ask, false); cerr == nil {
			_, code, err = a.p.run(cmd, nil, 30*time.Second)
		} else {
			err = cerr
		}
	}
	if err != nil || code != 0 {
		u.logf("Rollback failed: code %d %v", code, err)
		u.gui(func() {
			u.s3panel.show(toneStop, "Stopped: "+what+" The installer could not take the copied files out again. Sign in again and use \"Remove the label\", or reboot the printer and check.")
		})
		return
	}
	u.logf("Rolled back: %s", undone)
	u.gui(func() { u.s3panel.show(toneStop, "Stopped: "+what+" "+undone+nothingMore) })
}

func (a *app) askReboot(notNow string) {
	u := a.u
	if u.ask("Reboot the printer now?", "Reboot now", "Not now") {
		u.logf("Rebooting the printer")
		_, rcode, rerr := a.p.run(rebootCmd, nil, 60*time.Second)
		u.logf("Reboot command: code %d, %v (a lost connection is expected here)", rcode, rerr)
		a.p.close()
		a.signedIn = false
		u.gui(func() {
			u.s3bullets[3].set(iconOK, u.s3bullets[3].txt.Label())
			u.s3state.SetLabel("Rebooting. Remember: if the printer does not join your Wi-Fi by itself, enter the Wi-Fi password again on the touch screen. You can close this window.")
			u.done3 = true
		})
	} else {
		u.logf("Not rebooting")
		u.gui(func() { u.s3state.SetLabel("Not rebooting. " + notNow) })
	}
}

func (a *app) remove() {
	a.work(func() {
		u := a.u
		if !u.ask("Remove the ACE label from the printer?", "Remove", "Cancel") {
			u.logf("Remove cancelled. Nothing was changed.")
			return
		}
		u.logf("Removing the ACE touch-screen label")
		c, err := a.p.check()
		if err != nil {
			u.gui(func() { u.s3panel.show(toneStop, "Stopped: could not read the printer's state ("+shortErr(err)+")."+nothingMore) })
			return
		}
		switch c.state {
		case "standby", "complete", "cancelled", "error":
		case "printing", "paused":
			u.gui(func() { u.s3panel.show(toneStop, "Stopped: the printer is "+c.state+". Wait until it is idle."+nothingMore) })
			return
		default:
			if !u.ask("Could not read the printer's state.\n\nIs the printer idle (not printing)?", "Yes, it is idle", "No") {
				u.gui(func() { u.s3panel.show(toneStop, "Stopped: not confirmed idle."+nothingMore) })
				return
			}
		}
		_, code, err := a.p.run(removeCmd, nil, 60*time.Second)
		if err != nil || code != 0 {
			u.logf("Remove failed: code %d %v", code, err)
			u.gui(func() { u.s3panel.show(toneStop, "Stopped: could not remove the label files ("+errText(err)+"). Sign in again and check.") })
			return
		}
		u.logf("Removed the label file and its start-up hook")
		msg := "Removed the label file and its start-up hook.\nThe label is gone after the next reboot."
		if c.note == "yes" {
			if u.ask("This installer also switched on \"keep system changes across reboots\" (/oem/.debug).\nOther things you installed on the printer may depend on it.\n\nSwitch that off again too?", "Switch it off", "Leave it on") {
				_, code, err := a.p.run(switchOffCmd, nil, 60*time.Second)
				if err != nil || code != 0 {
					u.logf("Switch-off failed: code %d %v", code, err)
					msg += "\nCould not switch off keep-system-changes (" + errText(err) + "); it may still be on."
				} else {
					u.logf("Switched off /oem/.debug and removed the note file")
					msg += "\nSwitched off: the printer goes back to clearing system changes at each boot."
				}
			} else {
				u.logf("Left /oem/.debug on")
				msg += "\nKeep system changes was left on."
			}
		}
		u.gui(func() {
			u.installB.SetLabel("Install")
			for _, b := range u.s3bullets {
				b.set(iconDot, b.txt.Label())
			}
			u.s3panel.show(toneOK, msg)
		})
		a.askReboot("The label is gone after the next reboot.")
	})
}
