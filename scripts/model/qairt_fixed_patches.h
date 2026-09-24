// Direct fixed-shape Qwen image packing. No image/result caching or resizing.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dark_factory {
class FixedPatches {
public:
    static constexpr size_t height = 256, width = 448, patch = 16, merge = 2, temporal = 2;
    static constexpr size_t values = height * width * 3 * temporal;

    FixedPatches(float rescale, const std::vector<float>& mean, const std::vector<float>& stddev) {
        if (!std::isfinite(rescale) || mean.size() != 3 || stddev.size() != 3)
            throw std::runtime_error("Invalid fixed image normalization");
        for (size_t c = 0; c < 3; ++c) {
            if (!std::isfinite(mean[c]) || !std::isfinite(stddev[c]) || stddev[c] <= 0)
                throw std::runtime_error("Invalid fixed image normalization");
            for (size_t v = 0; v < 256; ++v) {
                // Preserve the reference's separately rounded float operations.
                // Do not compile with fast-math or fuse multiply/subtract.
                const float scaled = static_cast<float>(v) * rescale;
                const float centered = scaled - mean[c];
                table_[c][v] = centered / stddev[c];
                if (!std::isfinite(table_[c][v]))
                    throw std::runtime_error("Nonfinite fixed image normalization");
            }
        }
    }

    void write(const uint8_t* rgba, size_t size, float* output, size_t capacity) const {
        if (!rgba || size != height * width * 4 || !output || capacity != values)
            throw std::runtime_error("Invalid fixed image dimensions");
        for (size_t i = 0; i < size; i += 4)
            if (rgba[i] != rgba[i + 1] || rgba[i] != rgba[i + 2] || rgba[i + 3] != 255)
                throw std::runtime_error("Expected opaque grayscale sensor pixels");
        size_t offset = 0;
        // Final layout: [grid_t, block_y, block_x, merge_y, merge_x, C, T, P_y, P_x].
        for (size_t by = 0; by < height / (patch * merge); ++by)
            for (size_t bx = 0; bx < width / (patch * merge); ++bx)
                for (size_t my = 0; my < merge; ++my)
                    for (size_t mx = 0; mx < merge; ++mx)
                        for (size_t c = 0; c < 3; ++c)
                            for (size_t t = 0; t < temporal; ++t)
                                for (size_t py = 0; py < patch; ++py) {
                                    const size_t y = (by * merge + my) * patch + py;
                                    const size_t x = (bx * merge + mx) * patch;
                                    const uint8_t* row = rgba + (y * width + x) * 4;
                                    for (size_t px = 0; px < patch; ++px)
                                        output[offset++] = table_[c][row[px * 4]];
                                }
    }
private:
    std::array<std::array<float, 256>, 3> table_{};
};
}  // namespace dark_factory
