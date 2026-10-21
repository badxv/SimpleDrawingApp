#pragma once

#include <cstddef>

// Returns the path to features.ini next to the executable (empty on failure).
void GetFeaturesIniPath(char* path, size_t pathChars);

// Shared [Features] section accessors — single I/O path for flags + brush prefs.
bool FeaturesIniWrite(const char* key, const char* value);
int FeaturesIniReadInt(const char* key, int defaultValue);
