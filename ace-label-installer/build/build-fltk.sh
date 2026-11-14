#!/bin/bash
# Builds FLTK 1.4.5 static libraries for linux/amd64 (X11 only, no OpenGL, no Wayland, no Pango) and
# windows/amd64 (MinGW-w64, no OpenGL), offline in the container. Output: deps/fltk-out/{linux,windows}.
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
podman run --rm --network=none -v "$W/deps:/deps" localhost/u1li-build:1 bash -c '
set -euo pipefail
GOFLTK=$(ls -d /deps/gomod/github.com/pwiecz/go-fltk@*)
rm -rf /tmp/f && mkdir -p /tmp/f && cd /tmp/f
python3 -I -c "import tarfile; tarfile.open(\"/deps/fltk/fltk-1.4.5-source.tar.bz2\").extractall(\"/tmp/f\")"
cd fltk-1.4.5 && git apply -p1 $GOFLTK/lib/fltk-1.4.patch && cd ..
COMMON="-DCMAKE_BUILD_TYPE=Release -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_FLUID=OFF
 -DFLTK_BUILD_FLTK_OPTIONS=OFF -DFLTK_BUILD_HTML_DOCS=OFF -DFLTK_BUILD_PDF_DOCS=OFF -DFLTK_BUILD_GL=OFF
 -DFLTK_USE_SYSTEM_LIBJPEG=OFF -DFLTK_USE_SYSTEM_LIBPNG=OFF -DFLTK_USE_SYSTEM_ZLIB=OFF -DFLTK_BUILD_SHARED_LIBS=OFF"
cmake -S fltk-1.4.5 -B bl $COMMON -DFLTK_BACKEND_WAYLAND=OFF -DFLTK_BACKEND_X11=ON -DFLTK_USE_PANGO=OFF \
  -DFLTK_USE_XFT=ON -DFLTK_USE_LIBDECOR_GTK=OFF -DFLTK_GRAPHICS_CAIRO=OFF -DCMAKE_INSTALL_PREFIX=/tmp/f/il > /deps/logs/fltk-cmake-linux.txt
cmake --build bl --parallel 12 > /deps/logs/fltk-build-linux.txt
cmake --install bl > /dev/null
cat > /tmp/f/mingw.cmake <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
cmake -S fltk-1.4.5 -B bw $COMMON -DCMAKE_TOOLCHAIN_FILE=/tmp/f/mingw.cmake -DCMAKE_INSTALL_PREFIX=/tmp/f/iw > /deps/logs/fltk-cmake-windows.txt
cmake --build bw --parallel 12 > /deps/logs/fltk-build-windows.txt
cmake --install bw > /dev/null
rm -rf /deps/fltk-out && mkdir -p /deps/fltk-out
cp -r /tmp/f/il /deps/fltk-out/linux
cp -r /tmp/f/iw /deps/fltk-out/windows
ls /deps/fltk-out/linux/lib /deps/fltk-out/windows/lib
'
