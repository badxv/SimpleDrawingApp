#pragma once

#include <cstdint>
#include <vector>

struct AbrComputedParams {
    float diameter = 32.0f;
    float hardness = 1.0f;   // 0..1
    float roundness = 1.0f;    // 0..1
    float angleDeg = 0.0f;
    int spacing = 25;        // percent 1..1000, stored as PS percent
};

// Builds a grayscale row-major mask for a round/elliptical computed brush tip.
bool RasterizeComputedMask(const AbrComputedParams& params, int size, std::vector<std::uint8_t>& out);
