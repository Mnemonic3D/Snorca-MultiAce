#!/bin/bash
# Rebuilds the Linux FLTK 1.4.5 libraries on Debian 11 (glibc 2.31), offline. Replaces deps/fltk-out/linux.
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
podman run --rm --network=none -v "$W/deps:/deps" localhost/u1li-build-bullseye:1 bash -c '
set -euo pipefail
GOFLTK=$(ls -d /deps/gomod/github.com/pwiecz/go-fltk@*)
rm -rf /tmp/f && mkdir -p /tmp/f && cd /tmp/f
python3 -I -c "import tarfile; tarfile.open(\"/deps/fltk/fltk-1.4.5-source.tar.bz2\").extractall(\"/tmp/f\")"
cd fltk-1.4.5 && git apply -p1 $GOFLTK/lib/fltk-1.4.patch && cd ..
cmake -S fltk-1.4.5 -B bl -DCMAKE_BUILD_TYPE=Release -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_FLUID=OFF \
 -DFLTK_BUILD_FLTK_OPTIONS=OFF -DFLTK_BUILD_HTML_DOCS=OFF -DFLTK_BUILD_PDF_DOCS=OFF -DFLTK_BUILD_GL=OFF \
 -DFLTK_USE_SYSTEM_LIBJPEG=OFF -DFLTK_USE_SYSTEM_LIBPNG=OFF -DFLTK_USE_SYSTEM_ZLIB=OFF -DFLTK_BUILD_SHARED_LIBS=OFF \
 -DFLTK_BACKEND_WAYLAND=OFF -DFLTK_BACKEND_X11=ON -DFLTK_USE_PANGO=OFF -DFLTK_USE_XFT=ON -DFLTK_USE_LIBDECOR_GTK=OFF \
 -DFLTK_GRAPHICS_CAIRO=OFF -DCMAKE_INSTALL_PREFIX=/tmp/f/il > /deps/logs/fltk-cmake-linux-bullseye.txt
cmake --build bl --parallel 12 > /deps/logs/fltk-build-linux-bullseye.txt
cmake --install bl > /dev/null
rm -rf /deps/fltk-out/linux && cp -r /tmp/f/il /deps/fltk-out/linux
ls /deps/fltk-out/linux/lib
'
