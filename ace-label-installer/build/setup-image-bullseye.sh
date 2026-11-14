#!/bin/bash
# Older build image (Debian 11, glibc 2.31) for the Linux program, so it also runs on older distributions.
set -euo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
podman rm -f u1li-setup-b >/dev/null 2>&1 || true
podman run --name u1li-setup-b -v "$W/deps/logs:/logs" docker.io/library/debian:bullseye-slim bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
printf "deb http://archive.debian.org/debian bullseye main\ndeb http://archive.debian.org/debian-security bullseye-security main\n" > /etc/apt/sources.list
rm -f /etc/apt/sources.list.d/*
cp /etc/apt/sources.list /logs/apt-sources-bullseye.txt
apt-get update -q > /logs/apt-update-b1.txt
apt-get install -y -q --no-install-recommends strace > /logs/apt-strace-b.txt
strace -f -qq -e trace=connect -o /logs/apt-connect-bullseye.strace bash -c "
apt-get update -q && apt-get install -y -q --no-install-recommends \
  ca-certificates gcc g++ make cmake pkg-config file git python3 \
  libx11-dev libxext-dev libxft-dev libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev libfontconfig1-dev
" > /logs/apt-install-bullseye.txt
dpkg-query -W -f="\${Package}\t\${Version}\t\${Architecture}\n" > /logs/dpkg-list-bullseye.tsv
apt-get clean
'
podman commit u1li-setup-b localhost/u1li-build-bullseye:1
podman rm u1li-setup-b
podman image inspect docker.io/library/debian:bullseye-slim --format '{{.Digest}}' > "$W/deps/logs/bullseye-image-digest.txt"
echo "image ready"
