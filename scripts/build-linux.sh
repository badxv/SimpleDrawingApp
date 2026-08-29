#!/usr/bin/env bash
#
# build-linux.sh - Cross-compile SimpleDrawingApp into a Windows .exe on Linux.
#
# The project is a native Win32 + GDI+ app that officially builds with Visual
# Studio 2022 on Windows (see README.md). This helper lets the Cloud Agent /
# CI build the exact same sources on Linux using the MinGW-w64 cross toolchain,
# so the app can be run under Wine for verification.
#
# Requirements (installed by the environment update script):
#   mingw-w64  (x86_64-w64-mingw32-g++, x86_64-w64-mingw32-windres)
#
# Usage:
#   scripts/build-linux.sh                # builds to build-linux/SimpleDrawingApp.exe
#   OUT_DIR=/tmp/out scripts/build-linux.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/SimpleDrawingApp"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/build-linux}"
SHIM="$OUT_DIR/shim"

CXX="${CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"

command -v "$CXX" >/dev/null 2>&1 || { echo "ERROR: $CXX not found. Install mingw-w64." >&2; exit 1; }
command -v "$WINDRES" >/dev/null 2>&1 || { echo "ERROR: $WINDRES not found. Install mingw-w64." >&2; exit 1; }

rm -rf "$OUT_DIR"
mkdir -p "$SHIM"

# ---------------------------------------------------------------------------
# Case-sensitivity shims.
# The sources use includes that only resolve on Windows' case-insensitive FS:
#   - targetver.h includes <SDKDDKVer.h>  (MinGW ships lowercase sdkddkver.h)
#   - SimpleDrawingApp.rc includes "resource.h" (file is Resource.h)
# We provide correctly-cased symlinks on an include path instead of editing src.
# ---------------------------------------------------------------------------
MINGW_INC="$(dirname "$("$CXX" -print-file-name=include/sdkddkver.h 2>/dev/null || true)")"
if [ ! -f "$MINGW_INC/sdkddkver.h" ]; then
  # Fall back to the standard Debian/Ubuntu location.
  MINGW_INC="/usr/x86_64-w64-mingw32/include"
fi
ln -sf "$MINGW_INC/sdkddkver.h" "$SHIM/SDKDDKVer.h"
ln -sf "$SRC/Resource.h" "$SHIM/resource.h"

CXXFLAGS="-std=c++17 -O2 -DMINGW_HAS_SECURE_API=1 -I$SHIM -I$SRC"

echo "== Compiling sources =="
OBJS=()
for f in AtelierFonts AtelierArtwork AtelierControls AtelierPalette ColorPicker DrawingTools FileManager LayerHistory LayerStack UiChrome AppState EventBus UiFramework UiChromeLayout UiChromeRender UiControls UiPaletteFloat UiShapeFlyout UiToolbar AppCanvas AbrImport BrushEngine AppStroke AppViewport AppShell AppDialogs AppFeatureFlags AppCommands AppWindow CaptionBar AppSelection AppDocument SimpleDrawingApp; do
  echo "  cc $f.cpp"
  "$CXX" $CXXFLAGS -c "$SRC/$f.cpp" -o "$OUT_DIR/$f.o"
  OBJS+=("$OUT_DIR/$f.o")
done

echo "== Compiling resources =="
# The .rc is saved as UTF-16 (Visual Studio default); windres needs UTF-8.
iconv -f UTF-16 -t UTF-8 "$SRC/SimpleDrawingApp.rc" -o "$OUT_DIR/SimpleDrawingApp.rc"
"$WINDRES" --codepage=65001 -I"$SHIM" -I"$SRC" \
  "$OUT_DIR/SimpleDrawingApp.rc" -O coff -o "$OUT_DIR/resource.res"

echo "== Linking =="
"$CXX" -O2 -mwindows -static \
  "${OBJS[@]}" "$OUT_DIR/resource.res" \
  -lgdiplus -lcomctl32 -lcomdlg32 -ldwmapi -lmsimg32 -lgdi32 -luser32 -lole32 \
  -o "$OUT_DIR/SimpleDrawingApp.exe"

echo "== Bundling fonts =="
mkdir -p "$OUT_DIR/fonts"
cp -f "$REPO_ROOT/fonts/"*.ttf "$OUT_DIR/fonts/" 2>/dev/null || true
cp -f "$REPO_ROOT/fonts/"NOTICE.txt "$OUT_DIR/fonts/" 2>/dev/null || true
cp -f "$REPO_ROOT/fonts/"OFL-*.txt "$OUT_DIR/fonts/" 2>/dev/null || true

echo "== Bundling artwork =="
mkdir -p "$OUT_DIR/artwork"
cp -f "$REPO_ROOT/artwork/"*.png "$OUT_DIR/artwork/" 2>/dev/null || true
cp -f "$REPO_ROOT/artwork/"NOTICE.txt "$OUT_DIR/artwork/" 2>/dev/null || true

echo "== Done =="
echo "Built: $OUT_DIR/SimpleDrawingApp.exe"
file "$OUT_DIR/SimpleDrawingApp.exe"
