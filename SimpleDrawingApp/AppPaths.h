#pragma once

#include <cstddef>
#include <windows.h>

// Sibling file next to the executable (empty on failure).
void GetModuleSiblingPathW(wchar_t* path, size_t pathChars, const wchar_t* fileName);

// features.ini next to the executable (empty on failure).
void GetFeaturesIniPathW(wchar_t* path, size_t pathChars);

// Shared [Features] section accessors — single I/O path for flags + brush prefs.
bool FeaturesIniWrite(const wchar_t* key, const wchar_t* value);
bool FeaturesIniWriteA(const char* key, const char* value);
int FeaturesIniReadInt(const wchar_t* key, int defaultValue);
int FeaturesIniReadIntA(const char* key, int defaultValue);
