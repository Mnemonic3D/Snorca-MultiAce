package main

import (
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
)

// The two files of the ACE touch-screen label, exactly as released next to install.sh.
// The checksums below are the same ones install.sh checks.

//go:embed payload/libace-label.so
var libFile []byte

//go:embed payload/40-ace-label.sh
var hookFile []byte

const (
	guiSHA  = "bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4" // /usr/bin/gui, PAXX extended firmware 2.0.0 build 24
	libSHA  = "932b3efb6da6b9904cb13e95ab60b745ee1bfd727fe0883935b30a7328440de7"
	hookSHA = "1db28192dbb016a935c2aefb817c522054dc4521bb8fab498342b71baa27f0a1"

	libDst  = "/usr/local/lib/libace-label.so"
	hookDst = "/etc/hooks/lmd.d/40-ace-label.sh"
	markDst = "/oem/.debug"
	noteDst = "/oem/.ace-label-made-debug" // remembers that this installer (or install.sh) created the marker
)

func sha256hex(b []byte) string {
	s := sha256.Sum256(b)
	return hex.EncodeToString(s[:])
}

// payloadOK reports whether the files built into this program are undamaged.
func payloadOK() (bool, string) {
	if sha256hex(libFile) != libSHA {
		return false, "libace-label.so inside this installer is damaged or changed (checksum does not match)."
	}
	if sha256hex(hookFile) != hookSHA {
		return false, "40-ace-label.sh inside this installer is damaged or changed (checksum does not match)."
	}
	return true, ""
}
