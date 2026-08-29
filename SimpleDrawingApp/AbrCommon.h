#pragma once

#include <string>

// Basename without extension, or fallback when path is empty.
std::string LeafNameFromPath(const char* path, const char* fallback = "Imported");

// Combines the ABR file basename with a per-brush leaf name (e.g. "file.abr-001").
std::string MakeAbrBrushName(const char* path, const std::string& leafName);
