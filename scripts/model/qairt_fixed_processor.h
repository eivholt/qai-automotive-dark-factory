// Fixed grayscale sensor specialization of the Qwen processor layout/token splice.
// Layout/token conventions follow Qualcomm geniex-proc 2ec6abc (BSD-3-Clause).
// See qairt_score_worker.NOTICE. STB comes from that same pinned source checkout.
#pragma once
#include <cstring>
#include <fstream>
#include <memory>
#include "qairt_fixed_patches.h"
#include "geniex-proc/qwen2vl.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb/stb_image.h"

namespace dark_factory {
class FixedProcessor {
public:
    explicit FixedProcessor(const geniex::qwen2vl::Qwen2VLConfig& config)
        : patches_(config.rescale_factor, config.image_mean, config.image_std) {
        if (config.fixed_height != 256 || config.fixed_width != 448 || config.patch_size != 16 ||
            config.temporal_patch_size != 2 || config.merge_size != 2)
            throw std::runtime_error("Fixed processor requires the 448x256/P16/T2/M2 sensor profile");
    }

    void pixels(const std::vector<std::string>& paths, std::vector<float>& output) const {
        if (paths.empty() || paths.size() > 3) throw std::runtime_error("Expected one to three sensor images");
        output.resize(paths.size() * FixedPatches::values);
        for (size_t i = 0; i < paths.size(); ++i) {
            // Same pinned STB decoder, with bounded dimensions and no alpha loss.
            std::ifstream file(paths[i], std::ios::binary);
            unsigned char header[25]{};
            const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
            const unsigned char ihdr[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R'};
            if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) ||
                std::memcmp(header, png, sizeof(png)) != 0 ||
                std::memcmp(header + 8, ihdr, sizeof(ihdr)) != 0 || header[24] != 8)
                throw std::runtime_error("Expected 8-bit sensor PNG");
            int width = 0, height = 0, channels = 0;
            if (!stbi_info(paths[i].c_str(), &width, &height, &channels) || width != 448 || height != 256 ||
                stbi_is_16_bit(paths[i].c_str()))
                throw std::runtime_error("Expected 448x256 8-bit sensor PNG");
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image(
                stbi_load(paths[i].c_str(), &width, &height, &channels, 4), stbi_image_free);
            if (!image || width != 448 || height != 256) throw std::runtime_error("Sensor PNG decode failed");
            patches_.write(image.get(), 448 * 256 * 4, output.data() + i * FixedPatches::values,
                           FixedPatches::values);
        }
    }

    static std::vector<int32_t> tokens(geniex::Tokenizer& tokenizer, const std::string& text, size_t images) {
        constexpr char marker[] = "<__image__>";
        std::vector<int32_t> result;
        size_t cursor = 0, count = 0;
        while (cursor < text.size()) {
            const size_t hit = text.find(marker, cursor);
            const size_t end = hit == std::string::npos ? text.size() : hit;
            if (end > cursor) {
                const auto segment = tokenizer.encode(text.substr(cursor, end - cursor), false);
                result.insert(result.end(), segment.begin(), segment.end());
            }
            if (hit == std::string::npos) break;
            if (++count > images) throw std::runtime_error("Too many image markers");
            result.push_back(151652);
            result.insert(result.end(), 112, 151655);
            result.push_back(151653);
            cursor = hit + sizeof(marker) - 1;
        }
        if (count != images) throw std::runtime_error("Too few image markers");
        return result;
    }
private:
    FixedPatches patches_;
};
}  // namespace dark_factory
