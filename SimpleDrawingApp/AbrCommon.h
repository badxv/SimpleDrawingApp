#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <windows.h>

// Basename without extension, or fallback when path is empty.
std::string LeafNameFromPath(const wchar_t* path, const char* fallback = "Imported");

// Combines the ABR file basename with a per-brush leaf name (e.g. "file.abr-001").
std::string MakeAbrBrushName(const wchar_t* path, const std::string& leafName);

// Narrow a path segment for brush display names (UTF-8).
std::string NarrowPathSegment(const wchar_t* wide);

// Decode little-endian UCS-2 (Photoshop ABR) into a UTF-8 string.
// `raw` holds `charCount` code units × 2 bytes; stops at the first NUL.
std::string DecodeUcs2ToUtf8(const std::uint8_t* raw, std::size_t charCount);
