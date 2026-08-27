# AGENTS.md

## Cursor Cloud specific instructions

### What this is
Single product: **Simple Drawing App**, a native **Windows Win32 + GDI+** desktop
app (C++17). There is no backend, database, web server, package manager, test
suite, or CI. The canonical build is Visual Studio 2022 on Windows (see
`README.md`). Everything runs in one `SimpleDrawingApp.exe` process.

### Running it on the Linux Cloud VM
The app is Windows-only, so on this Linux VM it is **cross-compiled with
MinGW-w64** and **run under Wine on a headless Xvfb display**. Helper scripts:

- Install (Cloud Agent env): `scripts/cloud-agent-install.sh` — mingw-w64, wine,
  xvfb, fluxbox, xdotool (idempotent)
- Start (Cloud Agent env): `scripts/cloud-agent-start.sh` — Xvfb+fluxbox on `:99`
- Build:  `scripts/build-linux.sh`  → `build-linux/SimpleDrawingApp.exe`
- Run:    `scripts/run-linux.sh`     (auto-builds, starts Xvfb+fluxbox on `:99`, launches under Wine)

### Non-obvious gotchas
- **Case-sensitivity shims (do not "fix" the source):** the sources use
  `#include <SDKDDKVer.h>` (MinGW ships `sdkddkver.h`) and the `.rc` uses
  `#include "resource.h"` (file is `Resource.h`). `build-linux.sh` resolves these
  with correctly-cased symlinks on an include path — it does not edit the code.
- **`.rc` is UTF-16:** Visual Studio saved `SimpleDrawingApp.rc` as UTF-16, which
  `windres` cannot parse. The build script converts a copy to UTF-8 first.
- **Secure CRT:** the code uses `sprintf_s`, so the build defines
  `-DMINGW_HAS_SECURE_API=1`.
- **Wine window automation:** when driving the app with `xdotool`, you must call
  `xdotool windowactivate --sync <wid>` before `type`/`key`, otherwise keystrokes
  (including typing a filename into the Save/Open dialog) are silently dropped.
  Toolbar buttons/swatches are small targets — compute screen coords from the
  window's client origin rather than eyeballing a (possibly rescaled) screenshot.
  `Ctrl+N/O/S`, `Ctrl+Z` (undo), `Ctrl+Y` (redo) accelerators work reliably.
- The Wine prefix is created automatically on first `wine` run
  (`WINEPREFIX=$HOME/.wine-sda`); the dark-title-bar `DwmSetWindowAttribute` call
  is a harmless no-op under Wine.

### Lint / test / build
- **Lint:** no linter is configured. The closest proxy is compiling with
  warnings, e.g. `x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra ...` (see the
  flags in `scripts/build-linux.sh`).
- **Test:** there is no automated test suite. Verify manually by running the app
  (draw a stroke, undo, and Save a PNG).
- **Build/run:** use `scripts/build-linux.sh` / `scripts/run-linux.sh` here, or
  Visual Studio 2022 (`Debug|x64`) on Windows per `README.md`.
  MinGW does not define Windows `min`/`max` macros the same way MSVC does — prefer
  `NOMINMAX` (see `framework.h`) and avoid bare `std::min`/`std::max` near Win32 headers.

### Cloud Agent environment
Use the dashboard-managed environment with:
- `install`: `./scripts/cloud-agent-install.sh`
- `start`: `./scripts/cloud-agent-start.sh`

After Save, new agents should boot with MinGW/Wine already available (snapshot /
environment build) and only need a quick install refresh.
