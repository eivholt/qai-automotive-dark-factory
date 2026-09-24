// Actual pinned PNG decoder plus the production fixed processor; no QNN/model.
#include "../scripts/model/qairt_fixed_processor.h"
#include <cassert>
#include <iostream>

template<class F> void rejects(F call) {
    bool caught = false;
    try { call(); } catch (const std::runtime_error&) { caught = true; }
    assert(caught);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string directory = std::string(argv[1]) + "/";
    geniex::qwen2vl::Qwen2VLConfig config;
    config.fixed_width = 448; config.fixed_height = 256; config.patch_size = 16;
    dark_factory::FixedProcessor processor(config);
    std::vector<float> reference, actual;
    processor.pixels({directory + "L.png"}, reference);
    assert(reference.size() == dark_factory::FixedPatches::values);
    for (const auto* mode : {"LA", "RGB", "RGBA"}) {
        processor.pixels({directory + mode + ".png"}, actual);
        assert(actual == reference);
    }
    for (size_t count : {2, 3}) {
        processor.pixels(std::vector<std::string>(count, directory + "RGBA.png"), actual);
        assert(actual.size() == reference.size() * count);
        for (size_t i = 0; i < count; ++i)
            assert(std::equal(reference.begin(), reference.end(), actual.begin() + i * reference.size()));
    }
    for (const auto* name : {"color.png", "alpha.png", "wrong-size.png", "uint16.png", "one-bit.png",
                             "truncated.png", "wrong-format.png", "missing.png"}) {
        // Include a valid preceding image to exercise partial-batch failure.
        rejects([&] { processor.pixels({directory + "L.png", directory + name}, actual); });
        processor.pixels({directory + "L.png"}, actual);
        assert(actual == reference);
    }
    rejects([&] { processor.pixels({}, actual); });
    rejects([&] { processor.pixels(std::vector<std::string>(4, directory + "L.png"), actual); });
    config.patch_size = 14;
    rejects([&] { dark_factory::FixedProcessor wrong_profile(config); });
    std::cout << "fixed PNG decode, contract and recovery checks passed\n";
}
