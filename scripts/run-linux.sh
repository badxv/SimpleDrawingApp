#!/usr/bin/env bash
#
# run-linux.sh - Build (if needed) and run SimpleDrawingApp under Wine on a
# headless virtual X display (Xvfb). Intended for Cloud Agent / CI verification
# of this Windows-only app on Linux.
#
# Requirements (installed by the environment update script):
#   mingw-w64, wine / wine64, xvfb, fluxbox, x11-utils
#
# Environment:
#   DISPLAY_NUM   virtual display number (default: 99)
#   WINEPREFIX    wine prefix dir (default: $HOME/.wine-sda)
#
# Usage:
#   scripts/run-linux.sh          # starts Xvfb+fluxbox (if needed) and launches app
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/build-linux}"
EXE="$OUT_DIR/SimpleDrawingApp.exe"
DISPLAY_NUM="${DISPLAY_NUM:-99}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-sda}"
export WINEDEBUG="${WINEDEBUG:--all}"

if [ ! -f "$EXE" ]; then
  echo "== exe not found, building =="
  "$REPO_ROOT/scripts/build-linux.sh"
fi

# Start a virtual X display + window manager if not already running.
if ! DISPLAY=":$DISPLAY_NUM" xdpyinfo >/dev/null 2>&1; then
  echo "== starting Xvfb on :$DISPLAY_NUM =="
  Xvfb ":$DISPLAY_NUM" -screen 0 1280x800x24 -ac +extension GLX +render -noreset \
    > /tmp/xvfb-$DISPLAY_NUM.log 2>&1 &
  sleep 3
  DISPLAY=":$DISPLAY_NUM" fluxbox > /tmp/fluxbox-$DISPLAY_NUM.log 2>&1 &
  sleep 2
fi

export DISPLAY=":$DISPLAY_NUM"
echo "== launching SimpleDrawingApp.exe under Wine (DISPLAY=$DISPLAY) =="
cd "$OUT_DIR"
exec wine "$EXE"
