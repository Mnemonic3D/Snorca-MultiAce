#!/bin/bash
# Expendable GitHub runner only. No printer pairing or print commands.
set -euo pipefail
EVIDENCE="$PWD/package-test-results"
mkdir -p "$EVIDENCE"
(cd dist && shasum -a 256 -c ./*.sha256)
DMG=$(find dist -name '*.dmg' -print -quit)
MOUNT="$RUNNER_TEMP/snorca-check-dmg"
mkdir -p "$MOUNT"
hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MOUNT"
APP="$RUNNER_TEMP/Snorca checked install/FOrcaSlicer.app"
mkdir -p "$(dirname "$APP")"
ditto "$MOUNT/FOrcaSlicer.app" "$APP"
hdiutil detach "$MOUNT"
codesign --verify --deep --strict --verbose=2 "$APP"
PROFILE="$RUNNER_TEMP/snorca-check-empty-profile"
test ! -e "$PROFILE"
mkdir -p "$PROFILE"
printf '%s\n' '{"app":{"log_severity_level":"trace"}}' > "$PROFILE/Snapmaker_Orca.conf"
"$APP/Contents/MacOS/FOrcaSlicer" --datadir "$PROFILE" >"$EVIDENCE/stdout.txt" 2>"$EVIDENCE/stderr.txt" &
PID=$!
trap 'kill "$PID" 2>/dev/null || true' EXIT
sleep 30
screencapture -x "$EVIDENCE/home-30s.png"
sleep 60
screencapture -x "$EVIDENCE/home-90s.png"
ps -axo pid,ppid,command | grep -E 'FOrcaSlicer|snapmaker_control' >"$EVIDENCE/processes.txt" || true
if test -d "$PROFILE/log"; then cp -R "$PROFILE/log" "$EVIDENCE/app-log"; fi
ls -l "$APP/Contents/Resources"/snapmaker_control_macos_* >"$EVIDENCE/helper-modes.txt"
kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
pkill -f '/Contents/Resources/snapmaker_control_macos_' || true
if test "$PACKAGE_ARCH" = arm64; then HELPER=snapmaker_control_macos_arm64; else HELPER=snapmaker_control_macos_x64; fi
codesign --verify --verbose=2 "$APP/Contents/Resources/$HELPER" >"$EVIDENCE/helper-signature.txt" 2>&1 || true
"$APP/Contents/Resources/$HELPER" --locale=en_US --orca >"$EVIDENCE/helper-stdout.txt" 2>"$EVIDENCE/helper-stderr.txt" &
HELPER_PID=$!
sleep 15
ps -p "$HELPER_PID" -o pid,comm >"$EVIDENCE/direct-helper-process.txt" || true
kill "$HELPER_PID" 2>/dev/null || true
wait "$HELPER_PID" 2>/dev/null || true
echo 'Diagnostic run only; screenshots and helper logs require review, not an automatic release pass.' >"$EVIDENCE/scope.txt"
