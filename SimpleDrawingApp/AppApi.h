#pragma once

// Modern C++ API names. Legacy Do* entry points remain for compatibility.
#include "AppDocument.h"
#include "AppSelection.h"

inline void SaveDocument(HWND hwnd) { DoSave(hwnd); }
inline void OpenDocument(HWND hwnd) { DoOpen(hwnd); }
inline void NewDocument(HWND hwnd) { DoNew(hwnd); }
inline void UndoDocument(HWND hwnd) { DoUndo(hwnd); }
inline void RedoDocument(HWND hwnd) { DoRedo(hwnd); }
inline void ResizeCanvas(HWND hwnd) { DoCanvasSize(hwnd); }

inline void CopySelection(HWND hwnd) { DoCopy(hwnd); }
inline void CutSelection(HWND hwnd) { DoCut(hwnd); }
inline void PasteSelection(HWND hwnd) { DoPaste(hwnd); }
inline void DeleteSelection(HWND hwnd) { DoDeleteSelection(hwnd); }
inline void SelectAll(HWND hwnd) { DoSelectAll(hwnd); }
