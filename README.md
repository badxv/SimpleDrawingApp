# Simple Drawing App

A lightweight freehand drawing app for Windows, built with native **Win32** and **GDI+**.

Sketch with a pen, adjust color and thickness, then save or load images — no frameworks, no installers beyond Visual Studio.

## Features

- Freehand drawing with antialiased strokes and round pen tips
- Pen width control (slider + numeric box, 1–50)
- System color picker
- Save / load **PNG**, **JPG**, and **BMP**
- Resizable canvas (content preserved on window resize)
- Mouse capture for continuous strokes outside the window
- Simple menu: **File → Exit**, **Help → About**

## Screenshots

> Screenshots coming soon. Run the app locally and feel free to contribute images under `docs/`.

## Requirements

| Item | Detail |
|------|--------|
| OS | Windows 10 or later |
| IDE | Visual Studio 2022 (v143 toolset) |
| SDK | Windows 10 SDK |
| Language | C++ (Win32, MultiByte / ANSI APIs) |

## Build

1. Clone the repository:

   ```bash
   git clone https://github.com/badxv/SimpleDrawingApp.git
   cd SimpleDrawingApp
   ```

2. Open `SimpleDrawingApp.sln` in Visual Studio.

3. Select **Debug | x64** (or **Release | x64**).

4. Build with **Ctrl+Shift+B**, or run with **F5**.

Output binary (Debug):

```text
x64\Debug\SimpleDrawingApp.exe
```

## Usage

| Action | How |
|--------|-----|
| Draw | Drag with the left mouse button on the canvas (below the toolbar) |
| Pen width | Move the slider or type a value in the edit box |
| Color | Click **Color** |
| Save | Click **Save**, choose PNG / JPG / BMP |
| Load | Click **Load**, pick an image file |
| About | **Help → About** |
| Exit | **File → Exit** |

## Project structure

```text
SimpleDrawingApp/
├── SimpleDrawingApp.sln
└── SimpleDrawingApp/
    ├── SimpleDrawingApp.cpp   # Window, toolbar, drawing loop
    ├── SimpleDrawingApp.h
    ├── FileManager.cpp/.h     # Image save / load (GDI+)
    ├── ColorPicker.cpp/.h     # ChooseColor dialog
    ├── Resource.h / .rc       # Menu, About dialog, icons
    └── framework.h
```

## Roadmap

Planned improvements inspired by classic Paint and modern open-source editors (Pinta, RPaint, Win11 Paint):

- [ ] Modern toolbar UI and status bar
- [ ] Undo / redo
- [ ] Eraser and flood fill
- [ ] Shape tools (line, rectangle, ellipse)
- [ ] Keyboard shortcuts (Ctrl+S, Ctrl+Z, …)
- [ ] Selection, zoom, and clipboard
- [ ] Layers and transparency (later)

## Contributing

Issues and pull requests are welcome. For larger changes, open an issue first so we can align on scope.

## License

No license file is published yet. All rights reserved by the repository owner unless otherwise stated.
