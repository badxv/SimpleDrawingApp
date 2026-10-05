#pragma once

#include "AbrImport.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Parses a Photoshop 'desc' block payload and appends computed brushes.
bool ParseDescComputedBrushes(const std::uint8_t* data, size_t len, std::vector<AbrBrush>& out);
