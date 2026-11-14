package main

// #cgo linux,amd64 CPPFLAGS: -I${SRCDIR}/../deps/go-fltk-local/include/linux/amd64
// #cgo windows,amd64 CPPFLAGS: -I${SRCDIR}/../deps/go-fltk-local/include/windows/amd64
// #cgo CXXFLAGS: -std=c++11
// void ace_no_prefs_files(void);
import "C"

func noPrefsFiles() { C.ace_no_prefs_files() }
