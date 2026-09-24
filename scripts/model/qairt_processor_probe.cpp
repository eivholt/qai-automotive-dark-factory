// Offline equivalence/timing harness; never loads or dispatches a model graph.
#include <chrono>
#include <iostream>
#include "qwen3_vl/qwen3_vl.h"
#include "qairt_fixed_processor.h"

using Clock = std::chrono::steady_clock;
using json = qualla::json;
static double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: qairt_processor_probe BUNDLE");
        auto meta = geniex::parseQAIRTMetadata(argv[1]);
        if (!meta.vision_preprocessing) throw std::runtime_error("Missing vision profile");
        const auto& vp = *meta.vision_preprocessing;
        geniex::qwen2vl::Qwen2VLConfig config;
        config.fixed_height = vp.image_height; config.fixed_width = vp.image_width;
        config.patch_size = vp.patch_size; config.temporal_patch_size = vp.temporal_patch_size;
        config.merge_size = vp.spatial_merge_size;
        config.image_mean = vp.normalize_mean; config.image_std = vp.normalize_std;
        auto reference = geniex::qwen2vl::Qwen2VLProcessor::create(std::string(argv[1]) + "/tokenizer.json", config);
        dark_factory::FixedProcessor candidate(config);
        std::cout << json({{"ready", true}, {"schema", "fixed-processor-probe-v1"}}).dump() << std::endl;
        std::string line;
        while (std::getline(std::cin, line)) {
            json request, reply;
            try {
                if (line.size() > 65536) throw std::runtime_error("Request too large");
                request = json::parse(line);
                const auto paths = request.at("images").get<std::vector<std::string>>();
                const auto text = request.at("formatted_text").get<std::string>();
                if (paths.empty() || paths.size() > 3 || text.size() > 8192)
                    throw std::runtime_error("Invalid processor probe input size");
                std::vector<float> actual, expected;
                std::vector<int32_t> actual_tokens, expected_tokens;
                geniex::BatchFeatures bf;
                double reference_ms = 0, candidate_ms = 0;
                const auto run_reference = [&] {
                    auto start = Clock::now();
                    bf = reference->process(text, paths);
                    expected.assign(bf.pixel_values.cbegin(), bf.pixel_values.cend());
                    expected_tokens.assign(bf.input_ids.cbegin(), bf.input_ids.cend());
                    reference_ms = ms(start);
                };
                const auto run_candidate = [&] {
                    auto start = Clock::now();
                    candidate.pixels(paths, actual);
                    actual_tokens = candidate.tokens(reference->tokenizer(), text, paths.size());
                    candidate_ms = ms(start);
                };
                if (request.value("candidate_first", false)) { run_candidate(); run_reference(); }
                else { run_reference(); run_candidate(); }
                bool same_grid = bf.image_grid_thw.dimension() == 2 && bf.image_grid_thw.shape()[0] == paths.size() &&
                    bf.image_grid_thw.shape()[1] == 3;
                if (same_grid) for (size_t i = 0; i < paths.size(); ++i)
                    same_grid = same_grid && bf.image_grid_thw(i, 0) == 1 &&
                        bf.image_grid_thw(i, 1) == 16 && bf.image_grid_thw(i, 2) == 28;
                const bool same_pixels = actual.size() == expected.size() &&
                    std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(float)) == 0;
                size_t differing = 0;
                double max_delta = 0;
                if (actual.size() == expected.size()) for (size_t i = 0; i < actual.size(); ++i) {
                    differing += std::memcmp(&actual[i], &expected[i], sizeof(float)) != 0;
                    max_delta = std::max(max_delta, std::abs(double(actual[i]) - expected[i]));
                }
                reply = {{"same_pixels", same_pixels}, {"same_tokens", actual_tokens == expected_tokens},
                    {"same_grid", same_grid}, {"float_count", actual.size()}, {"differing_floats", differing},
                    {"max_abs_delta", max_delta}, {"reference_ms", reference_ms}, {"candidate_ms", candidate_ms}};
            } catch (const std::exception& e) { reply = {{"error", e.what()}}; }
            reply["request_id"] = request.is_object() && request.contains("request_id") && request["request_id"].is_string()
                ? request["request_id"] : json("");
            std::cout << reply.dump() << std::endl;
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
