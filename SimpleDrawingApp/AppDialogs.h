#pragma once

#include <windows.h>

void ShowAboutDialog(HWND owner);
void ShowShortcutsDialog(HWND owner);
// Returns true if the user confirmed a name (written to outName). Does not mutate layers.
bool PromptLayerRename(HWND owner, char* outName, size_t outChars);
