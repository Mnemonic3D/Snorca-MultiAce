#!/bin/bash
# Downloads the FLTK 1.4.5 source release into deps/fltk inside the container (connections traced).
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$W/deps/fltk"
podman run --rm -v "$W/deps:/deps" localhost/u1li-build:1 bash -c '
cd /deps/fltk
strace -f -qq -e trace=connect -o /deps/logs/fltk-connect.strace python3 -I -c "
import urllib.request, hashlib
u = \"https://github.com/fltk/fltk/releases/download/release-1.4.5/fltk-1.4.5-source.tar.bz2\"
d = urllib.request.urlopen(u, timeout=120).read()
open(\"fltk-1.4.5-source.tar.bz2\", \"wb\").write(d)
print(u, len(d), hashlib.sha256(d).hexdigest())
" | tee /deps/logs/fltk-download.txt
'
