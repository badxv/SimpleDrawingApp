#!/usr/bin/env bash
#
# Idempotent Cloud Agent install: MinGW cross-compile + Wine GUI smoke tooling.
# Safe to re-run. Does not start Xvfb or the app (see cloud-agent-start.sh /
# run-linux.sh).
#
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

PACKAGES=(
  mingw-w64
  g++-mingw-w64-x86-64
  wine64
  wine
  xvfb
  fluxbox
  xdotool
  x11-utils
  ffmpeg
)

echo "== cloud-agent-install: apt packages =="
sudo apt-get update -y
sudo apt-get install -y --no-install-recommends "${PACKAGES[@]}"

echo "== cloud-agent-install: toolchain check =="
command -v x86_64-w64-mingw32-g++ >/dev/null
command -v wine >/dev/null
command -v Xvfb >/dev/null
command -v fluxbox >/dev/null
command -v xdotool >/dev/null
command -v xdpyinfo >/dev/null
x86_64-w64-mingw32-g++ --version | head -1
wine --version

# Warm a Wine prefix once so first app launch is faster (no-op if already set up).
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-sda}"
export WINEDEBUG="${WINEDEBUG:--all}"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
  echo "== cloud-agent-install: initializing Wine prefix at $WINEPREFIX =="
  wineboot -u >/tmp/wineboot-sda.log 2>&1 || true
fi

echo "== cloud-agent-install: done =="
