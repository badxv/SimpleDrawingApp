#include "AbrComputed.h"

#include <cmath>
#include <algorithm>

namespace {

float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float RadialAlpha(float dx, float dy, float hardness) {
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d >= 1.0f) return 0.0f;
    const float h = Clamp01(hardness);
    if (d <= h) return 1.0f;
    if (h >= 1.0f) return 0.0f;
    return 1.0f - (d - h) / (1.0f - h);
}

} // namespace

bool RasterizeComputedMask(const AbrComputedParams& params, int size, std::vector<std::uint8_t>& out) {
    if (size < 8 || size > 512) return false;
    out.assign(static_cast<size_t>(size) * static_cast<size_t>(size), 0);

    const float hardness = Clamp01(params.hardness);
    const float roundness = std::max(0.05f, Clamp01(params.roundness));
    const float angleRad = params.angleDeg * 3.14159265f / 180.0f;
    const float cosA = std::cos(angleRad);
    const float sinA = std::sin(angleRad);

    const float cx = (size - 1) * 0.5f;
    const float cy = (size - 1) * 0.5f;
    const float radius = (size - 2) * 0.5f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = (static_cast<float>(x) - cx) / radius;
            float dy = (static_cast<float>(y) - cy) / radius;
            const float rx = dx * cosA + dy * sinA;
            const float ry = (-dx * sinA + dy * cosA) / roundness;
            const float a = RadialAlpha(rx, ry, hardness);
            out[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)]
                = static_cast<std::uint8_t>(a * 255.0f + 0.5f);
        }
    }
    return true;
}
