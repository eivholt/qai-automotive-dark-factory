#include "../scripts/model/qairt_fixed_patches.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

template<class F> void rejects(F call) {
    bool caught = false;
    try { call(); } catch (const std::runtime_error&) { caught = true; }
    assert(caught);
}

int main() {
    using dark_factory::FixedPatches;
    const float scale = 1.0f / 255.0f;
    const std::vector<float> mean{.48145466f, .4578275f, .40821073f};
    const std::vector<float> stddev{.26862954f, .26130258f, .27577711f};
    FixedPatches pack(scale, mean, stddev);
    std::vector<uint8_t> rgba(448 * 256 * 4, 255);
    for (size_t y = 0; y < 256; ++y) for (size_t x = 0; x < 448; ++x) {
        const uint8_t value = uint8_t(x * 37 + y * 83);
        for (size_t c = 0; c < 3; ++c) rgba[(y * 448 + x) * 4 + c] = value;
    }
    std::vector<float> actual(FixedPatches::values + 2, -999);
    pack.write(rgba.data(), rgba.size(), actual.data() + 1, FixedPatches::values);
    assert(actual.front() == -999 && actual.back() == -999);
    // Independent inverse indexing into the documented flattened tensor.
    for (size_t offset = 0; offset < FixedPatches::values; ++offset) {
        size_t k = offset;
        const size_t px = k % 16; k /= 16;
        const size_t py = k % 16; k /= 16;
        k /= 2; // identical temporal copies
        const size_t c = k % 3; k /= 3;
        const size_t mx = k % 2; k /= 2;
        const size_t my = k % 2; k /= 2;
        const size_t bx = k % 14;
        const size_t by = k / 14;
        const size_t x = (bx * 2 + mx) * 16 + px;
        const size_t y = (by * 2 + my) * 16 + py;
        const float scaled = float(rgba[(y * 448 + x) * 4 + c]) * scale;
        const float centered = scaled - mean[c];
        const float expected = centered / stddev[c];
        assert(std::memcmp(&actual[offset + 1], &expected, sizeof(float)) == 0);
    }
    rejects([&] { pack.write(nullptr, rgba.size(), actual.data(), FixedPatches::values); });
    rejects([&] { pack.write(rgba.data(), rgba.size() - 1, actual.data(), FixedPatches::values); });
    rejects([&] { pack.write(rgba.data(), rgba.size(), actual.data(), FixedPatches::values - 1); });
    rgba[1] ^= 1;
    rejects([&] { pack.write(rgba.data(), rgba.size(), actual.data(), FixedPatches::values); });
    rgba[1] ^= 1; rgba[3] = 254;
    rejects([&] { pack.write(rgba.data(), rgba.size(), actual.data(), FixedPatches::values); });
    rejects([&] { FixedPatches invalid(scale, mean, {1, 0, 1}); });
    rejects([&] { FixedPatches invalid(scale, {}, stddev); });
    rejects([&] { FixedPatches invalid(std::numeric_limits<float>::quiet_NaN(), mean, stddev); });
    rejects([&] { FixedPatches invalid(std::numeric_limits<float>::max(), mean, stddev); });
    std::cout << "fixed patch layout and contract checks passed\n";
}
