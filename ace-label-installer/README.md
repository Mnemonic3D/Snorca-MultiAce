# ACE screen label installer (experimental)

A small program with a window that puts the "ACE" touch-screen label on a Snapmaker U1, for people who do not want to do a firmware update or use a command line. It does the same steps as the `install.sh` in the label zip.

Ready-made programs for Windows and Linux are on the [Releases page](https://github.com/Mnemonic3D/Snorca-MultiAce/releases).

## What it does

1. **Find your printer.** You type the printer's IP address. It asks the printer (read-only) whether it is a U1 and whether it is idle.
2. **Sign in.** You type the printer's root password. It checks the firmware (PAXX extended firmware 2.0.0, build 24) and whether the printer keeps system changes. If it does not, it offers to unlock it; the printer reboots and you enter your Wi-Fi password again on its touch screen.
3. **Install.** It copies `libace-label.so` and `40-ace-label.sh`, checks both by checksum and asks before it reboots the printer. "Remove the label" takes them off again.

The password is kept in memory until the window closes. It is not written to disk and is sent only to the printer you typed in. The program makes no other connections.

Off switch at any time, no sign-in needed: create `config/extended/ace-label.disabled` in Fluidd or Mainsail.

## What it needs

- Snapmaker U1 with PAXX extended firmware 2.0.0, build 24, SSH switched on.
- Windows 64-bit, or Linux 64-bit with X11 (glibc 2.31 or newer).

## Building

`src/` is Go (1.22) with an FLTK 1.4.5 window through go-fltk. The scripts in `build/` fetch FLTK and the Go modules and build both programs in podman containers; `build/build.sh` is the last step and runs with no network. The two files in `src/payload/` are the same as in the label zip.

## How far it is tested

Run by the author from Linux on one of his own printers, which was already unlocked. The unlock step and the Windows program have only been run against a simulated printer. Use at your own risk.
