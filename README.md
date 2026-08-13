# Simple Drawing App

A lightweight drawing app for Windows, built with native **Win32** and **GDI+**.

Sketch with pen, eraser, and fill tools; undo your work; save or load images — no heavy frameworks.

## Features

- **Tools:** Pen, Eraser, Flood Fill
- **Color:** preset swatches + system color picker
- **Pen width:** slider / edit box / `[` `]` keys (1–50)
- **Undo / Redo** (stroke & fill history)
- **New / Clear** with unsaved-change prompts
- **Save / Open** PNG, JPG, BMP
- Resizable canvas, mouse capture, status bar
- Modern light chrome + dark title bar (Windows 10/11)
- Keyboard shortcuts: `Ctrl+N/O/S`, `Ctrl+Z/Y`

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
| Color | Click a swatch or **Color...** |
| Width | Slider, box, or `[` / `]` |
| Undo / Redo | Buttons, menu, or `Ctrl+Z` / `Ctrl+Y` |
| Save / Open | Buttons, menu, or `Ctrl+S` / `Ctrl+O` |

## Project structure

```text
SimpleDrawingApp/
├── SimpleDrawingApp.sln
├── README.md
└── SimpleDrawingApp/
    ├── SimpleDrawingApp.cpp   # Window, toolbar, input
    ├── CanvasHistory.*        # Undo / redo snapshots
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
- [ ] Shape tools (line, rectangle, ellipse)
- [ ] Selection, zoom, clipboard
- [ ] Layers and transparency

## Contributing

Issues and pull requests are welcome. Prefer small, focused changes.

## License

No license file is published yet. All rights reserved by the repository owner unless otherwise stated.
