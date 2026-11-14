#!/bin/bash
# Downloads Go modules into deps/gomod inside the container, network connections traced.
# Usage: fetch-mods.sh <go args...>
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
podman run --rm -v /usr/lib/go-1.22:/usr/lib/go-1.22:ro -v /usr/share/go-1.22:/usr/share/go-1.22:ro -v "$W:/w" -w /w/src \
  -e GOMODCACHE=/w/deps/gomod -e GOPATH=/tmp/gopath -e GOCACHE=/tmp/gocache -e GOTOOLCHAIN=local \
  -e GOFLAGS=-modcacherw -e GOTELEMETRY=off -e HOME=/tmp \
  localhost/u1li-build:1 bash -c '
export PATH=/usr/lib/go-1.22/bin:$PATH
go telemetry off 2>/dev/null || true
n=$(date +%H%M%S)
strace -f -qq -e trace=connect -o /w/deps/logs/go-connect-$n.strace go "$@"
' bash "$@"
