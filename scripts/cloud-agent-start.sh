#!/usr/bin/env bash
#
# Per-boot Cloud Agent start: ensure headless Xvfb + fluxbox on :99.
# Idempotent; returns after the display is ready (does not launch the app).
#
set -euo pipefail

DISPLAY_NUM="${DISPLAY_NUM:-99}"
export DISPLAY=":$DISPLAY_NUM"

if ! xdpyinfo >/dev/null 2>&1; then
  echo "== cloud-agent-start: starting Xvfb on :$DISPLAY_NUM =="
  Xvfb ":$DISPLAY_NUM" -screen 0 1280x800x24 -ac +extension GLX +render -noreset \
    >/tmp/xvfb-$DISPLAY_NUM.log 2>&1 &
  for _ in $(seq 1 30); do
    if xdpyinfo >/dev/null 2>&1; then
      break
    fi
    sleep 0.2
  done
  if ! xdpyinfo >/dev/null 2>&1; then
    echo "Xvfb failed to become ready on :$DISPLAY_NUM" >&2
    exit 1
  fi
fi

if ! pgrep -x fluxbox >/dev/null 2>&1; then
  echo "== cloud-agent-start: starting fluxbox =="
  fluxbox >/tmp/fluxbox-$DISPLAY_NUM.log 2>&1 &
  sleep 0.5
fi

echo "== cloud-agent-start: DISPLAY=$DISPLAY ready =="
