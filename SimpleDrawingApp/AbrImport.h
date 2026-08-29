#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AbrSampledBrush {
    std::string name;
    int spacing = 25;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask; // grayscale, row-major
};

// Parses Photoshop .abr sampled brushes (v1/v2 and v6/v10 samp sections).
bool LoadAbrSampledBrushes(const char* path, std::vector<AbrSampledBrush>& out, std::string& error);
