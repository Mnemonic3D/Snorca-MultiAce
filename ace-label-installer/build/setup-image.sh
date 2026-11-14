#!/bin/bash
# Builds the local build/test container image localhost/u1li-build:1 from debian:bookworm-slim.
# Every network connection after the first "apt-get install strace" is traced; logs go to deps/logs.
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
podman rm -f u1li-setup >/dev/null 2>&1 || true
podman run --name u1li-setup -v "$W/deps/logs:/logs" docker.io/library/debian:bookworm-slim bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
cp /etc/apt/sources.list.d/debian.sources /logs/apt-sources.txt
apt-get update -q > /logs/apt-update-1.txt
apt-get install -y -q --no-install-recommends strace > /logs/apt-strace.txt
strace -f -qq -e trace=connect -o /logs/apt-connect.strace bash -c "
apt-get update -q && apt-get install -y -q --no-install-recommends \
  ca-certificates git gcc g++ make cmake pkg-config file \
  gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix mingw-w64-x86-64-dev \
  libx11-dev libxext-dev libxft-dev libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev libfontconfig1-dev \
  xvfb xdotool x11-apps x11-utils netpbm fonts-dejavu-core \
  dropbear-bin busybox python3 iproute2 procps psmisc
" > /logs/apt-install.txt
dpkg-query -W -f="\${Package}\t\${Version}\t\${Architecture}\n" > /logs/dpkg-list.tsv
apt-get clean
'
podman commit u1li-setup localhost/u1li-build:1
podman rm u1li-setup
echo "image ready"
