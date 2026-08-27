#pragma once

#include <windows.h>

bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
void ClearCanvas(HWND hwnd, bool pushHistory);
void FlattenLayers(HWND hwnd);
void FlipDocumentHorizontal(HWND hwnd);
void FlipDocumentVertical(HWND hwnd);
void RotateDocument90Cw(HWND hwnd);
void ResizeCanvas(HWND hwnd);
bool PromptSaveIfDirty(HWND hwnd);
void SaveDocument(HWND hwnd);
void SaveDocumentAs(HWND hwnd);
void ExportDocument(HWND hwnd);
void PrintDocument(HWND hwnd);
void AutosaveIfNeeded(HWND hwnd);
bool OfferAutosaveRecovery(HWND hwnd);
void OpenDocument(HWND hwnd);
void OpenLastDocument(HWND hwnd);
void OpenRecentDocument(HWND hwnd, int index);
bool LastDocumentAvailable();
void ClearRecentDocuments();
void SyncRecentFileMenu(HMENU fileMenu);
void LoadSessionState();
void NewDocument(HWND hwnd);
void UndoDocument(HWND hwnd);
void RedoDocument(HWND hwnd);
