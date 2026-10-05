#pragma once

#include <cstddef>

// Returns the path to features.ini next to the executable (empty on failure).
void GetFeaturesIniPath(char* path, size_t pathChars);
