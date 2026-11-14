#!/bin/bash
# Builds the Linux program (on Debian 11, glibc 2.31) and the Windows program (MinGW-w64 on Debian 12),
# both offline (no network) in containers. Output in out/.
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
GO="-v /usr/lib/go-1.22:/usr/lib/go-1.22:ro -v /usr/share/go-1.22:/usr/share/go-1.22:ro"
ENV="-e GOMODCACHE=/w/deps/gomod -e GOPATH=/tmp/gopath -e GOCACHE=/tmp/gocache -e GOTOOLCHAIN=local -e GOPROXY=off -e GOFLAGS=-mod=mod -e GOTELEMETRY=off -e HOME=/tmp -e CGO_ENABLED=1"
TAGS="netgo,osusergo"
podman run --rm --network=none $GO -v "$W:/w" -w /w/src $ENV localhost/u1li-build-bullseye:1 bash -c '
set -euo pipefail
export PATH=/usr/lib/go-1.22/bin:$PATH
go vet -tags '"$TAGS"' ./...
GOOS=linux GOARCH=amd64 CC=gcc CXX=g++ \
  go build -tags '"$TAGS"' -trimpath -buildvcs=false -ldflags "-s -w -extldflags \"-static-libgcc -static-libstdc++\"" -o /w/out/ace-label-installer-linux-x64 .
cd /w/out
echo "--- linux needs (direct):"; objdump -p ace-label-installer-linux-x64 | grep NEEDED
echo "--- newest glibc symbol:"; objdump -T ace-label-installer-linux-x64 | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
'
podman run --rm --network=none $GO -v "$W:/w" -w /w/src $ENV localhost/u1li-build:1 bash -c '
set -euo pipefail
export PATH=/usr/lib/go-1.22/bin:$PATH
GOOS=windows GOARCH=amd64 CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix \
  go build -tags '"$TAGS"' -trimpath -buildvcs=false -ldflags "-s -w -H windowsgui -extldflags \"-static\"" -o /w/out/ace-label-installer-windows-x64.exe .
cd /w/out
echo "--- windows needs:"; x86_64-w64-mingw32-objdump -p ace-label-installer-windows-x64.exe | grep "DLL Name" | sort -u
file ace-label-installer-linux-x64 ace-label-installer-windows-x64.exe
ls -l; sha256sum ace-label-installer-linux-x64 ace-label-installer-windows-x64.exe
'
