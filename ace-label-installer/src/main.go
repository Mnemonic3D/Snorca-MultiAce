// ACE screen label installer for the Snapmaker U1 (PAXX extended firmware 2.0.0 build 24).
// It does what install.sh does, from this computer over SSH, in a small window. EXPERIMENTAL.
package main

import (
	"flag"
	"os"
	"runtime"

	"github.com/pwiecz/go-fltk"
)

func init() { runtime.LockOSThread() } // FLTK must run on the main thread

// fitScreen shrinks the whole window (text included) when the screen is too short for it, for example
// a laptop at 1366x768 or a high display scaling. The window keeps the approved layout, only smaller.
func fitScreen() {
	for i := 0; i < fltk.ScreenCount(); i++ {
		_, _, _, wh := fltk.ScreenWorkArea(i) // in window units at the current scale
		need := winH + 50                      // window plus its title bar
		if wh > 0 && wh < need {
			s := fltk.ScreenScale(i) * float32(wh) / float32(need)
			if s < 0.6 {
				s = 0.6
			}
			fltk.SetScreenScale(i, s)
		}
	}
}

func main() {
	wait := flag.Int("wait-seconds", 600, "how long to wait for the printer after the unlock reboot before asking")
	flag.Parse()
	if *wait < 10 {
		*wait = 10
	}
	a := &app{waitSeconds: *wait}
	noPrefsFiles()
	fltk.Lock() // enables fltk.Awake from the background work
	fitScreen()
	a.u = newUI(a)
	a.p = newPrinter(a.u.logf)
	a.u.logf("ACE screen label installer started (EXPERIMENTAL)")
	if ok, why := payloadOK(); !ok {
		a.u.logf("Built-in files damaged: %s", why)
		a.u.s1msg.show(toneStop, "This installer file is damaged: "+why+" Download it again.")
		a.u.checkB.Deactivate()
		a.u.ipIn.Deactivate()
	} else {
		a.u.logf("Built-in files checked: libace-label.so and 40-ace-label.sh match their checksums")
	}
	a.u.win.Show()
	fltk.AddTimeout(0.3, func() {
		// If the system placed the window so its bottom buttons fall below the usable screen, move it up.
		w := a.u.win
		n := fltk.ScreenNum(w.X()+w.W()/2, w.Y())
		sx, sy, sw, sh := fltk.ScreenWorkArea(n)
		a.u.logf("Screen work area %d,%d %dx%d, scale %.2f, window at %d,%d size %dx%d",
			sx, sy, sw, sh, fltk.ScreenScale(n), w.X(), w.Y(), w.W(), w.H())
		if w.Y()+w.H() > sy+sh {
			ny := sy + sh - w.H()
			if ny < sy+30 {
				ny = sy + 30
			}
			w.Resize(w.X(), ny, w.W(), w.H())
			a.u.logf("Moved the window up to %d,%d", w.X(), ny)
		}
	})
	a.u.ipIn.TakeFocus()
	fltk.Run()
	a.p.close()
	os.Exit(0)
}
