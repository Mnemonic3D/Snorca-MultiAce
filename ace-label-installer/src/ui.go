package main

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/pwiecz/go-fltk"
)

// Layout of the window (pixels). Kept fixed so the window looks like the approved mock-up.
const (
	winW, winH = 480, 780
	logH       = 200
	padX       = 20
	navY       = 722
)

var (
	colBlue     = fltk.Color(0x1a73e800)
	colLight    = fltk.Color(0xf1f3f400)
	colHeader   = fltk.Color(0x3c404300)
	colText     = fltk.Color(0x11111100)
	colBody     = fltk.Color(0x33333300)
	colGrey     = fltk.Color(0x5f636800)
	colGreen    = fltk.Color(0x18803800)
	colGreenTxt = fltk.Color(0x0d652d00)
	colGreenBg  = fltk.Color(0xe6f4ea00)
	colOrange   = fltk.Color(0xe3740000)
	colOrangeTx = fltk.Color(0x7a410000)
	colOrangeBg = fltk.Color(0xfff4e500)
	colDot      = fltk.Color(0x9aa0a600) // neutral grey: "not done yet" or a plain fact, never a warning
	colRed      = fltk.Color(0xc5221f00)
	colRedTxt   = fltk.Color(0xa50e0e00)
	colRedBg    = fltk.Color(0xfce8e600)
	colWhite    = fltk.Color(0xffffff00)
)

type icon int

const (
	iconNone icon = iota
	iconOK
	iconDot
	iconBad
)

// statusLine is one row with a drawn icon (check, dot or cross) and a text.
type statusLine struct {
	ico  *fltk.Box
	txt  *fltk.Box
	kind icon
}

func newStatusLine(x, y int) *statusLine {
	l := &statusLine{}
	l.ico = fltk.NewBox(fltk.NO_BOX, x, y+3, 18, 18)
	l.ico.SetDrawHandler(func(base func()) {
		X, Y := l.ico.X(), l.ico.Y()
		switch l.kind {
		case iconOK:
			fltk.SetDrawColor(colGreen)
			fltk.SetLineStyle(fltk.SOLID, 3)
			fltk.DrawLine(X+2, Y+9, X+7, Y+14)
			fltk.DrawLine(X+7, Y+14, X+16, Y+3)
			fltk.SetLineStyle(fltk.SOLID, 0)
		case iconDot:
			fltk.SetDrawColor(colDot)
			fltk.DrawPie(X+5, Y+5, 8, 8, 0, 360)
		case iconBad:
			fltk.SetDrawColor(colRed)
			fltk.SetLineStyle(fltk.SOLID, 3)
			fltk.DrawLine(X+3, Y+3, X+15, Y+15)
			fltk.DrawLine(X+15, Y+3, X+3, Y+15)
			fltk.SetLineStyle(fltk.SOLID, 0)
		}
	})
	l.txt = fltk.NewBox(fltk.NO_BOX, x+26, y, winW-x-26-padX, 24)
	l.txt.SetAlign(fltk.ALIGN_INSIDE | fltk.ALIGN_LEFT)
	l.txt.SetLabelSize(15)
	l.txt.SetLabelColor(colText)
	l.set(iconNone, "")
	return l
}

func (l *statusLine) set(k icon, text string) {
	l.kind = k
	l.txt.SetLabel(text)
	if k == iconNone && text == "" {
		l.ico.Hide()
		l.txt.Hide()
	} else {
		l.ico.Show()
		l.txt.Show()
	}
	l.ico.Redraw()
	l.txt.Redraw()
	if p := l.txt.Parent(); p != nil {
		p.Redraw()
	}
}

func label(x, y, w, h int, text string, size int, col fltk.Color) *fltk.Box {
	b := fltk.NewBox(fltk.NO_BOX, x, y, w, h, text)
	b.SetAlign(fltk.ALIGN_INSIDE | fltk.ALIGN_LEFT | fltk.ALIGN_TOP | fltk.ALIGN_WRAP)
	b.SetLabelSize(size)
	b.SetLabelColor(col)
	return b
}

func button(x, y, w int, text string, primary bool) *fltk.Button {
	b := fltk.NewButton(x, y, w, 38, text)
	b.SetLabelFont(fltk.HELVETICA_BOLD)
	b.SetLabelSize(15)
	b.SetBox(fltk.RFLAT_BOX)
	b.SetDownBox(fltk.RFLAT_BOX)
	if primary {
		b.SetColor(colBlue)
		b.SetSelectionColor(fltk.Color(0x1557b000))
		b.SetLabelColor(colWhite)
	} else {
		b.SetColor(fltk.Color(0xdadce000))
		b.SetSelectionColor(fltk.Color(0xc4c7c500))
		b.SetLabelColor(colText)
	}
	b.ClearVisibleFocus()
	return b
}

// panel is a coloured message box (caution, success, stop).
type panel struct{ box *fltk.Box }

func newPanel(x, y, w, h int) *panel {
	b := fltk.NewBox(fltk.BORDER_BOX, x, y, w, h)
	b.SetAlign(fltk.ALIGN_INSIDE | fltk.ALIGN_LEFT | fltk.ALIGN_TOP | fltk.ALIGN_WRAP)
	b.SetLabelSize(15)
	b.Hide()
	return &panel{b}
}

type tone int

const (
	toneCaution tone = iota
	toneOK
	toneStop
	toneInfo
)

func (p *panel) show(t tone, text string) {
	bg, fg, border := colOrangeBg, colOrangeTx, colOrange
	switch t {
	case toneOK:
		bg, fg, border = colGreenBg, colGreenTxt, colGreen
	case toneStop:
		bg, fg, border = colRedBg, colRedTxt, colRed
	case toneInfo:
		bg, fg, border = fltk.Color(0xe8f0fe00), fltk.Color(0x174ea600), colBlue
	}
	p.box.SetColor(bg)
	p.box.SetLabelColor(fg)
	p.box.SetSelectionColor(border)
	p.box.SetDrawHandler(func(base func()) {
		X, Y, W, H := p.box.X(), p.box.Y(), p.box.W(), p.box.H()
		fltk.DrawRectfWithColor(X, Y, W, H, bg)
		fltk.SetDrawColor(border)
		fltk.SetLineStyle(fltk.SOLID, 2)
		fltk.DrawRect(X+1, Y+1, W-2, H-2)
		fltk.SetLineStyle(fltk.SOLID, 0)
		fltk.SetDrawColor(fg)
		fltk.SetDrawFont(fltk.HELVETICA, 15)
		fltk.Draw(p.box.Label(), X+12, Y+10, W-24, H-20, fltk.ALIGN_INSIDE|fltk.ALIGN_LEFT|fltk.ALIGN_TOP|fltk.ALIGN_WRAP)
	})
	p.box.SetLabel(text)
	p.box.Show()
	p.box.Redraw()
}

func (p *panel) hide() { p.box.Hide(); redrawParent(p.box) }

func redrawParent(b *fltk.Box) {
	if pr := b.Parent(); pr != nil {
		pr.Redraw()
	}
}

// ---------- the window ----------

type ui struct {
	win      *fltk.Window
	header   *fltk.Box
	detailsB *fltk.Button
	logBox   *fltk.TextDisplay
	logBuf   *fltk.TextBuffer
	saveLogB *fltk.Button
	showLog  bool

	// step 1
	s1                       *fltk.Group
	ipIn                     *fltk.Input
	checkB, next1            *fltk.Button
	s1lines                  [4]*statusLine
	s1ok                     *panel
	s1msg                    *panel
	// step 2
	s2                       *fltk.Group
	s2intro                  *fltk.Box
	pwIn                     *fltk.SecretInput
	signB, unlockB           *fltk.Button
	s2hint                   *fltk.Box
	s2lines                  [5]*statusLine
	s2panel                  *panel
	s2wait                   *fltk.Box
	s2ipLabel                *fltk.Box
	s2ipIn                   *fltk.Input
	back2, next2             *fltk.Button
	// step 3
	s3                       *fltk.Group
	s3bullets                [4]*statusLine
	s3state                  *fltk.Box
	installB                 *fltk.Button
	s3panel                  *panel
	s3exp                    *fltk.Box
	removeB, closeB          *fltk.Button
	// question overlay
	q                        *fltk.Group
	qText                    *fltk.Box
	qYes, qNo                *fltk.Button
	qAnswer                  chan bool

	busy     bool
	done3    bool // the printer was rebooted from step 3: nothing more to do
	logMu    sync.Mutex
	logLines []string
	app      *app
}

func newUI(a *app) *ui {
	u := &ui{app: a}
	fltk.SetScheme("gtk+")
	fltk.SetBackgroundColor(255, 255, 255)
	fltk.SetBackground2Color(255, 255, 255)
	fltk.SetForegroundColor(0x11, 0x11, 0x11)

	// Place the window near the top of the screen so its bottom buttons are never under a task bar.
	if sx, sy, sw, sh := fltk.ScreenWorkArea(0); sw > 0 {
		x := sx + (sw-winW)/2
		y := sy + 32
		if extra := sh - winH - 32; extra > 64 {
			y = sy + 32 + extra/3
		}
		u.win = fltk.NewWindowWithPosition(x, y, winW, winH, "ACE screen label installer")
	} else {
		u.win = fltk.NewWindow(winW, winH, "ACE screen label installer")
	}
	u.win.SetColor(colWhite)
	u.header = fltk.NewBox(fltk.FLAT_BOX, 0, 0, winW, 38, "  Snapmaker U1: ACE screen label")
	u.header.SetColor(colHeader)
	u.header.SetLabelColor(colWhite)
	u.header.SetLabelFont(fltk.HELVETICA_BOLD)
	u.header.SetLabelSize(15)
	u.header.SetAlign(fltk.ALIGN_INSIDE | fltk.ALIGN_LEFT)
	u.detailsB = fltk.NewButton(winW-110, 7, 100, 24, "Show details")
	u.detailsB.SetBox(fltk.FLAT_BOX)
	u.detailsB.SetColor(colHeader)
	u.detailsB.SetLabelColor(fltk.Color(0xdadce000))
	u.detailsB.SetLabelSize(12)
	u.detailsB.ClearVisibleFocus()
	u.detailsB.SetCallback(u.toggleLog)

	u.buildStep1()
	u.buildStep2()
	u.buildStep3()
	u.buildQuestion()

	u.logBuf = fltk.NewTextBuffer()
	u.logBox = fltk.NewTextDisplay(padX, winH+6, winW-2*padX, logH-50)
	u.logBox.SetBuffer(u.logBuf)
	u.logBox.SetTextSize(11)
	u.logBox.SetWrapMode(fltk.WRAP_AT_BOUNDS)
	u.saveLogB = button(padX, winH+logH-40, 120, "Save log", false)
	u.saveLogB.SetLabelSize(13)
	u.saveLogB.Resize(padX, winH+logH-38, 120, 30)
	u.saveLogB.SetCallback(u.saveLog)
	u.win.End()

	u.win.SetCallback(u.onClose)

	u.show(1)
	return u
}

func (u *ui) title(text string) {
	t := fltk.NewBox(fltk.NO_BOX, padX, 52, winW-2*padX, 30, text)
	t.SetAlign(fltk.ALIGN_INSIDE | fltk.ALIGN_LEFT)
	t.SetLabelFont(fltk.HELVETICA_BOLD)
	t.SetLabelSize(19)
	t.SetLabelColor(colText)
}

func (u *ui) buildStep1() {
	u.s1 = fltk.NewGroup(0, 38, winW, winH-38)
	u.title("Step 1 of 3: find your printer")
	label(padX, 92, 440, 70, "Type the printer's IP address.\nYou can read it on the touch screen:\nSettings, Network.", 15, colBody)
	u.ipIn = fltk.NewInput(padX, 166, 300, 34)
	u.ipIn.SetCallbackCondition(fltk.WhenEnterKeyAlways)
	u.ipIn.SetCallback(func() { u.app.checkPrinter() })
	label(padX+310, 174, 140, 20, "for example 192.0.2.50", 12, colGrey)
	u.checkB = button(padX, 214, 150, "Check printer", true)
	u.checkB.SetCallback(func() { u.app.checkPrinter() })
	for i := range u.s1lines {
		u.s1lines[i] = newStatusLine(padX, 270+i*28)
	}
	u.s1ok = newPanel(padX, 270, 440, 96)     // green result box after a successful check
	u.s1msg = newPanel(padX, 306, 440, 130)   // red box: a real problem that stops here
	u.next1 = button(310, navY, 150, "Next", true)
	u.next1.Deactivate()
	u.next1.SetCallback(func() { u.app.toStep2() })
	u.s1.End()
}

const (
	s2introBefore = "Sign in so the installer can check the printer: its firmware, the ACE label, and whether it keeps system changes."
	s2introLocked = "The label has to stay after a reboot.\nFor that the printer must keep system\nchanges. Yours does not yet."
	cautionText   = "CAUTION\nUnlocking reboots the printer.\nAfter the reboot, ENTER YOUR WI-FI\nPASSWORD AGAIN on the touch screen.\nHave your Wi-Fi name and password\nready before you go on."
)

func (u *ui) buildStep2() {
	u.s2 = fltk.NewGroup(0, 38, winW, winH-38)
	u.title("Step 2 of 3: unlock the printer")
	u.s2intro = label(padX, 92, 440, 70, s2introBefore, 15, colBody)
	label(padX, 166, 440, 22, "Root password of the printer:", 15, colBody)
	u.pwIn = fltk.NewSecretInput(padX, 190, 300, 34)
	u.pwIn.SetCallbackCondition(fltk.WhenEnterKeyAlways)
	u.pwIn.SetCallback(func() { u.app.signIn() })
	u.signB = button(330, 188, 130, "Sign in", true)
	u.signB.SetCallback(func() { u.app.signIn() })
	u.s2hint = label(padX, 232, 440, 20, "Used only to sign in while this window is open. Never saved.", 14, colGrey)
	for i := range u.s2lines {
		u.s2lines[i] = newStatusLine(padX, 258+i*25)
	}
	u.s2panel = newPanel(padX, 388, 440, 160)
	u.unlockB = button(padX, 558, 200, "Unlock and reboot", true)
	u.unlockB.SetCallback(func() { u.app.unlock() })
	u.unlockB.Hide()
	u.s2wait = label(padX, 604, 440, 44, "", 14, colGrey)
	u.s2ipLabel = label(padX, 656, 150, 30, "Printer address now:", 14, colGrey)
	u.s2ipLabel.SetAlign(fltk.ALIGN_INSIDE | fltk.ALIGN_LEFT)
	u.s2ipIn = fltk.NewInput(padX+150, 656, 200, 30)
	u.s2ipLabel.Hide()
	u.s2ipIn.Hide()
	u.back2 = button(150, navY, 150, "Back", false)
	u.back2.SetCallback(func() { u.app.backToStep1() })
	u.next2 = button(310, navY, 150, "Next", true)
	u.next2.SetCallback(func() { u.app.next2() })
	u.s2.End()
}

func (u *ui) buildStep3() {
	u.s3 = fltk.NewGroup(0, 38, winW, winH-38)
	u.title("Step 3 of 3: install the label")
	label(padX, 92, 440, 22, "This will:", 15, colBody)
	for i, t := range []string{"copy the label file to the printer", "add its start-up hook", "check both files after copying", "reboot the printer (it asks you first)"} {
		u.s3bullets[i] = newStatusLine(padX, 118+i*28)
		u.s3bullets[i].set(iconDot, t)
	}
	u.s3state = label(padX, 232, 440, 40, "", 13, colGrey)
	u.installB = button(padX, 278, 150, "Install", true)
	u.installB.SetCallback(func() { u.app.install() })
	u.s3panel = newPanel(padX, 330, 440, 150)
	u.s3exp = label(padX, 494, 440, 90, "EXPERIMENTAL. Use at your own risk.\nOff switch any time, no sign-in needed:\ncreate config/extended/ace-label.disabled\nin Fluidd or Mainsail.", 14, colGrey)
	u.removeB = button(padX, navY, 180, "Remove the label", false)
	u.removeB.SetCallback(func() { u.app.remove() })
	u.closeB = button(310, navY, 150, "Close", true)
	u.closeB.SetCallback(func() { u.onClose() })
	u.s3.End()
}

func (u *ui) buildQuestion() {
	const qy = 496
	u.q = fltk.NewGroup(0, qy, winW, winH-qy)
	bg := fltk.NewBox(fltk.FLAT_BOX, 0, qy, winW, winH-qy)
	bg.SetColor(colWhite)
	frame := fltk.NewBox(fltk.NO_BOX, padX-6, qy+6, winW-2*padX+12, winH-qy-14)
	frame.SetDrawHandler(func(base func()) {
		fltk.DrawRectfWithColor(frame.X(), frame.Y(), frame.W(), frame.H(), fltk.Color(0xe8f0fe00))
		fltk.SetDrawColor(colBlue)
		fltk.SetLineStyle(fltk.SOLID, 2)
		fltk.DrawRect(frame.X()+1, frame.Y()+1, frame.W()-2, frame.H()-2)
		fltk.SetLineStyle(fltk.SOLID, 0)
	})
	u.qText = label(padX+6, qy+16, winW-2*padX-12, navY-6-qy-24, "", 14, fltk.Color(0x17305600))
	u.qNo = button(110, navY-6, 150, "No", false)
	u.qYes = button(270, navY-6, 190, "Yes", true)
	u.qNo.SetCallback(func() { u.answer(false) })
	u.qYes.SetCallback(func() { u.answer(true) })
	u.q.End()
	u.q.Hide()
}

// show switches to step n.
func (u *ui) show(n int) {
	u.s1.Hide()
	u.s2.Hide()
	u.s3.Hide()
	switch n {
	case 1:
		u.s1.Show()
	case 2:
		u.s2.Show()
	case 3:
		u.s3.Show()
	}
	u.win.Redraw()
}

func (u *ui) toggleLog() {
	u.showLog = !u.showLog
	h := winH
	if u.showLog {
		h += logH
		u.detailsB.SetLabel("Hide details")
	} else {
		u.detailsB.SetLabel("Show details")
	}
	u.win.Resize(u.win.X(), u.win.Y(), winW, h)
	u.win.Redraw()
}

// ---------- calls from the worker goroutine (thread-safe) ----------

// gui runs fn on the window's thread and waits until it has run.
func (u *ui) gui(fn func()) {
	done := make(chan struct{})
	fltk.Awake(func() { fn(); close(done) })
	<-done
}

func (u *ui) logf(format string, args ...any) {
	line := time.Now().Format("15:04:05 ") + fmt.Sprintf(format, args...)
	u.logMu.Lock()
	u.logLines = append(u.logLines, line)
	u.logMu.Unlock()
	fmt.Fprintln(os.Stdout, line)
	fltk.Awake(func() {
		u.logBuf.Append(line + "\n")
		u.logBox.SetInsertPosition(u.logBuf.Length())
		u.logBox.ShowInsertPosition()
	})
}

// ask shows a question over the window and waits for the answer. Safe to call from the worker only.
func (u *ui) ask(msg, yes, no string) bool {
	ch := make(chan bool, 1)
	u.gui(func() {
		u.qAnswer = ch
		u.qText.SetLabel(msg)
		u.qYes.SetLabel(yes)
		u.qNo.SetLabel(no)
		u.q.Show()
		u.win.Redraw()
	})
	u.logf("Question: %s", oneLine(msg))
	a := <-ch
	if a {
		u.logf("Answer: %s", yes)
	} else {
		u.logf("Answer: %s", no)
	}
	return a
}

func (u *ui) answer(a bool) {
	if u.qAnswer == nil {
		return
	}
	ch := u.qAnswer
	u.qAnswer = nil
	u.q.Hide()
	u.win.Redraw()
	ch <- a
}

func (u *ui) setBusy(b bool) {
	u.gui(func() {
		u.busy = b
		for _, w := range []interface{ Activate(); Deactivate() }{u.checkB, u.ipIn, u.signB, u.pwIn, u.back2, u.unlockB, u.installB, u.removeB} {
			if b || (u.done3 && (w == u.installB || w == u.removeB)) {
				w.Deactivate()
			} else {
				w.Activate()
			}
		}
		if b {
			u.next1.Deactivate()
			u.next2.Deactivate()
		}
		u.win.Redraw()
	})
}

func (u *ui) onClose() {
	if et := fltk.EventType(); (et == fltk.SHORTCUT || et == fltk.KEYDOWN) && fltk.EventKey() == fltk.ESCAPE {
		// Escape never closes the window (only the Close button or the window's X does); it answers an
		// open question with its "no" button.
		if u.qAnswer != nil {
			u.answer(false)
		}
		return
	}
	if u.busy {
		if u.qAnswer != nil {
			return // a question is open; answer it first
		}
		go func() {
			if u.ask("The installer is still working on the printer. Closing now could leave it half-changed.\n\nClose anyway?", "Close anyway", "Keep working") {
				fltk.Awake(func() { os.Exit(0) })
			}
		}()
		return
	}
	u.app.p.close()
	u.win.Hide()
}

func (u *ui) saveLog() {
	u.logMu.Lock()
	text := strings.Join(u.logLines, "\n") + "\n"
	u.logMu.Unlock()
	name := "ace-label-installer-log-" + time.Now().Format("2006-01-02-150405") + ".txt"
	var path string
	if runtime.GOOS == "windows" {
		c := fltk.NewNativeFileChooser()
		c.SetType(fltk.NativeFileChooser_BROWSE_SAVE_FILE)
		c.SetOptions(fltk.NativeFileChooser_SAVEAS_CONFIRM)
		c.SetTitle("Save the installer log")
		c.SetPresetFile(name)
		c.SetFilter("Text\t*.txt")
		if c.Show() != 0 || len(c.Filenames()) == 0 {
			c.Destroy()
			return
		}
		path = c.Filenames()[0]
		c.Destroy()
	} else {
		home, _ := os.UserHomeDir()
		p, ok := fltk.ChooseFile("Save the installer log", "*.txt", filepath.Join(home, name), false)
		if !ok || p == "" {
			return
		}
		path = p
	}
	if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
		u.logf("Could not save the log: %v", err)
		return
	}
	u.logf("Log saved to %s", path)
}
