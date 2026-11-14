#!/bin/bash
# Makes deps/go-fltk-local: the downloaded go-fltk module with OpenGL support removed, the prebuilt
# libraries it ships replaced by our own FLTK 1.4.5 build (deps/fltk-out), and only linux/windows amd64 kept.
set -euo pipefail
D="$(cd "$(dirname "$0")/.." && pwd)/deps"
SRC=$(ls -d "$D"/gomod/github.com/pwiecz/go-fltk@v0.0.0-20260522213023-3e944122e7b1)
L="$D/go-fltk-local"
[ -e "$L" ] && rm -rf "$D/go-fltk-local"
mkdir -p "$L"
cp -r "$SRC"/. "$L"/
chmod -R u+w "$L"
cd "$L"
rm -rf lib include examples
rm -f gl_window.go gl_window.cxx gl_window.h fltk-build.go fltk-build-invoke.go text_test.go widget_deletion_test.go
rm -f cgo_darwin_*.go cgo_openbsd_*.go cgo_linux_arm64.go cgo_linux_arm.go
mkdir -p include/linux/amd64 include/windows/amd64 lib/linux/amd64 lib/windows/amd64
cp -r "$D/fltk-out/linux/include/." include/linux/amd64/
cp -r "$D/fltk-out/windows/include/." include/windows/amd64/
cp "$D/fltk-out/linux/lib/"*.a lib/linux/amd64/
cp "$D/fltk-out/windows/lib/"*.a lib/windows/amd64/
cat > cgo_linux_amd64.go <<'EOF'
//go:build linux && amd64

package fltk

// #cgo linux,amd64 CXXFLAGS: -std=c++11
// #cgo linux,amd64 CPPFLAGS: -I${SRCDIR}/include/linux/amd64 -I${SRCDIR}/include/linux/amd64/FL/images -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_THREAD_SAFE -D_REENTRANT
// #cgo linux,amd64 LDFLAGS: ${SRCDIR}/lib/linux/amd64/libfltk_images.a ${SRCDIR}/lib/linux/amd64/libfltk_jpeg.a ${SRCDIR}/lib/linux/amd64/libfltk_png.a ${SRCDIR}/lib/linux/amd64/libfltk_z.a ${SRCDIR}/lib/linux/amd64/libfltk_forms.a ${SRCDIR}/lib/linux/amd64/libfltk.a -lm -lX11 -lXext -lpthread -lXinerama -lXfixes -lXcursor -lXft -lXrender -lfontconfig -ldl
import "C"
EOF
cat > cgo_windows_amd64.go <<'EOF'
//go:build windows && amd64

package fltk

// #cgo windows,amd64 CXXFLAGS: -std=c++11
// #cgo windows,amd64 CPPFLAGS: -I${SRCDIR}/include/windows/amd64 -I${SRCDIR}/include/windows/amd64/FL/images -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
// #cgo windows,amd64 LDFLAGS: -mwindows ${SRCDIR}/lib/windows/amd64/libfltk_images.a ${SRCDIR}/lib/windows/amd64/libfltk_jpeg.a ${SRCDIR}/lib/windows/amd64/libfltk_png.a ${SRCDIR}/lib/windows/amd64/libfltk_z.a ${SRCDIR}/lib/windows/amd64/libfltk_forms.a ${SRCDIR}/lib/windows/amd64/libfltk.a -lgdiplus -lole32 -luuid -lcomctl32 -lws2_32 -lwinspool -lgdi32 -lcomdlg32 -lshell32 -luser32
import "C"
EOF
grep -rln "Gl_Window\|gl_window\|GlWindow" . || echo "no GL references left"
