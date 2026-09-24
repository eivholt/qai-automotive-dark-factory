// Small fake of the protected QAIRT API for score-only lifecycle tests.
// No QNN execution, pixels or model accuracy is represented by this fixture.
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace qualla { using json = int; }
namespace geniex {
struct PixelData { std::vector<float> pixel_values; };
struct VLMInput { PixelData pixel_data; };
struct GenerationConfig { int max_tokens = 0; bool enable_sampling = true; bool thinking_mode = true; };
struct LLMSpec {
    size_t seq_len_prefill = 2, hidden_size = 2;
    std::vector<size_t> context_lengths{8};
    std::vector<int32_t> eos_token_ids{4};
};
struct InputProvider { virtual ~InputProvider() = default; };
struct PrecomputedEmbeddingProvider : InputProvider {
    std::vector<float> buffer;
    int clears = 0;
    std::vector<float> lookupBatch(const std::vector<int32_t>& tokens) const {
        std::vector<float> values;
        for (int32_t token : tokens) { values.push_back(float(token)); values.push_back(float(token)); }
        return values;
    }
    void setBuffer(std::vector<float> value, size_t offset) {
        if (offset != 0) throw std::runtime_error("Unexpected embedding offset");
        buffer = std::move(value);
    }
    void clearBuffer() { buffer.clear(); ++clears; }
};
namespace qwen3_vl {
class Qwen3VLModel {
public:
    explicit Qwen3VLModel(LLMSpec spec) : spec_(std::move(spec)) {
        input_providers_.push_back(std::make_unique<PrecomputedEmbeddingProvider>());
    }
    virtual ~Qwen3VLModel() = default;
    void resetKVCache() { n_past_ = 0; token_history_.clear(); ++reset_calls; positions_live = false; }
    size_t nPast() const { return n_past_; }
    PrecomputedEmbeddingProvider& embedding() {
        return *dynamic_cast<PrecomputedEmbeddingProvider*>(input_providers_.front().get());
    }
    void duplicateEmbedding() { input_providers_.push_back(std::make_unique<PrecomputedEmbeddingProvider>()); }
    void removeEmbedding() { input_providers_.clear(); }
    std::vector<float> vision{101, 102, 103, 104};
    std::vector<float> logits{0, 1, 2, 3, 4, 5, 20, 7};
    std::vector<float> seen_embeds;
    std::vector<int32_t> seen_tokens;
    int vision_calls = 0, reset_calls = 0, sample_calls = 0, position_clears = 0;
    mutable int read_calls = 0;
    bool fail_prefill = false, incomplete_prefill = false, fail_sample = false, positions_live = false;
protected:
    std::vector<float> encodeVision(const PixelData& pixels) {
        if (pixels.pixel_values.empty()) throw std::runtime_error("Missing pixels");
        ++vision_calls;
        return vision;
    }
    static void maskedScatter(std::vector<float>& text, const std::vector<float>& image,
                              const std::vector<int32_t>& tokens, int32_t image_id, size_t hidden) {
        size_t cursor = 0;
        for (size_t i = 0; i < tokens.size(); ++i) if (tokens[i] == image_id)
            for (size_t j = 0; j < hidden; ++j) text.at(i * hidden + j) = image.at(cursor++);
    }
    void preparePositions(const std::vector<int32_t>& tokens, const VLMInput&, size_t offset) {
        if (offset != 0) throw std::runtime_error("Unexpected position offset");
        seen_tokens = tokens;
        positions_live = true;
    }
    void clearPositions() { positions_live = false; ++position_clears; }
    void prepareSampler(const GenerationConfig& config, const std::vector<int32_t>&) {
        if (config.max_tokens != 1 || config.enable_sampling || config.thinking_mode)
            throw std::runtime_error("Wrong scoring configuration");
    }
    void prefillChunks(const std::vector<int32_t>& tokens, size_t* last) {
        if (!positions_live) throw std::runtime_error("Positions were not prepared");
        seen_embeds = embedding().buffer;
        n_past_ = tokens.size();
        token_history_ = tokens;
        if (fail_prefill) throw std::runtime_error("Simulated graph error after partial write");
        if (incomplete_prefill) --n_past_;
        *last = (tokens.size() - 1) % spec_.seq_len_prefill + 1;
    }
    int32_t sampleNextToken(size_t phase, size_t offset) {
        if (phase != 0 || offset != (n_past_ - 1) % spec_.seq_len_prefill || !positions_live)
            throw std::runtime_error("Wrong sampling phase, row or lifecycle");
        ++sample_calls;
        if (fail_sample) throw std::runtime_error("Simulated sampler error");
        std::vector<float> values;
        readLastLogits(phase, offset, values);
        return int32_t(std::max_element(values.begin(), values.end()) - values.begin());
    }
    void readLastLogits(size_t phase, size_t offset, std::vector<float>& values) const {
        if (phase != 0 || offset != (n_past_ - 1) % spec_.seq_len_prefill)
            throw std::runtime_error("Wrong output row");
        ++read_calls;
        values = logits;
    }
    LLMSpec spec_;
    std::vector<std::unique_ptr<InputProvider>> input_providers_;
    std::vector<int32_t> token_history_;
    size_t n_past_ = 0;
    int32_t image_token_id_ = 7;
};
}
}
