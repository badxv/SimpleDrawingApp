# Simple Drawing App

A lightweight drawing app for Windows, built with native **Win32** and **GDI+**.

Sketch with pen, eraser, and fill tools; undo your work; save or load images — no heavy frameworks.

## Features

- **Tools:** Pen, Eraser, Flood Fill, Line, Rectangle, Ellipse, Select
- **Color:** preset swatches + system color picker
- **Opacity:** 1–100% for pen, eraser, fill, and shapes (`Shift` + mouse wheel)
- **Pen width:** slider / edit box / mouse wheel / `[` `]` keys (1–50)
- **Shapes:** click-drag preview; hold `Shift` for square / circle / axis-aligned or 45° lines
- **Selection:** rectangular marquee, move, Delete; **Cut / Copy / Paste** (`Ctrl+X/C/V`)
- **Zoom:** `Ctrl` + mouse wheel, View menu (`Ctrl++` / `Ctrl+-` / `Ctrl+0` / Fit)
- **Layers:** stack panel with add/delete/reorder, visibility, per-layer opacity; content layers are PNG-like (alpha); Background stays pinned at the bottom (opaque canvas)
- **Undo / Redo** (layer-stack snapshots)
- **New / Clear** with unsaved-change prompts
- **Save / Open** PNG, JPG, BMP
- **Fixed document canvas** (default 1280×720) with scrollable viewport — resizing the window no longer crops artwork
- **Canvas Size…** (`Image` menu or `Ctrl+E`) to change document dimensions; open sets size to the loaded image
- Mouse capture, status bar with live size + zoom
- Modern light chrome + dark title bar (Windows 10/11)
- Keyboard shortcuts: `Ctrl+N/O/S/E/A/X/C/V`, `Ctrl+Z/Y`, `Del`, `Esc`

## Requirements

| Item | Detail |
|------|--------|
| OS | Windows 10 or later |
| IDE | Visual Studio 2022 (v143 toolset) |
| SDK | Windows 10 SDK |

## Build

```bash
git clone https://github.com/badxv/SimpleDrawingApp.git
cd SimpleDrawingApp
```

1. Open `SimpleDrawingApp.sln` in Visual Studio  
2. Select **Debug | x64** (or **Release | x64**)  
3. Build (**Ctrl+Shift+B**) or run (**F5**)

## Usage

| Action | How |
|--------|-----|
| Draw | Select **Pen**, drag on the canvas |
| Erase | Select **Eraser**, drag |
| Fill | Select **Fill**, click a region |
| Line / Rect / Ellipse | Select the tool, click-drag (`Shift` constrains) |
| Select / move | **Select**, drag a marquee; drag inside to move |
| Cut / Copy / Paste | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` (or Edit menu) |
| Zoom | `Ctrl` + mouse wheel, or **View** menu |
| Layers | Right panel: `+` / `-` / Up / Dn, Visible, opacity |
| Color | Click a swatch or **Color...** |
| Width | Slider, box, mouse wheel, or `[` / `]` |
| Opacity | Slider, box, or `Shift` + mouse wheel |
| Undo / Redo | Buttons, menu, or `Ctrl+Z` / `Ctrl+Y` |
| Save / Open | Buttons, menu, or `Ctrl+S` / `Ctrl+O` |
| Canvas size | **Image → Canvas Size…** or `Ctrl+E` |
| Scroll | Scrollbars when the document is larger than the viewport |

## Project structure

```text
SimpleDrawingApp/
├── SimpleDrawingApp.sln
├── README.md
└── SimpleDrawingApp/
    ├── SimpleDrawingApp.cpp   # Window, toolbar, input
    ├── CanvasHistory.*        # Legacy single-bitmap undo (unused by UI)
    ├── LayerStack.*           # Layer list + composite
    ├── LayerHistory.*         # Undo / redo for the full layer stack
    ├── DrawingTools.*         # Tool enum + flood fill
    ├── FileManager.*          # Image save / load
    ├── ColorPicker.*          # ChooseColor dialog
    └── Resource.h / .rc       # Menu, accelerators, About
```

## Roadmap

- [x] Modern toolbar + status bar
- [x] Undo / redo
- [x] Eraser + flood fill
- [x] Keyboard shortcuts + dirty prompt
- [x] Fixed document canvas + scroll viewport + Canvas Size
- [x] Shape tools (line, rectangle, ellipse)
- [x] Selection, zoom, clipboard
- [x] Layers and transparency

## Contributing

Issues and pull requests are welcome. Prefer small, focused changes.

## License

No license file is published yet. All rights reserved by the repository owner unless otherwise stated.
