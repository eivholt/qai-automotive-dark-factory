#define DF_QAIRT_SCORE_WORKER_TEST
#include "../scripts/model/qairt_score_worker.cpp"
#include <cassert>

template<class Function> void rejects(Function call) {
    bool rejected = false;
    try { call(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
}

int main() {
    const std::vector<int32_t> tokens{10, 7, 7, 11, 12};
    const VLMInput input{{{1, 2, 3}}};
    {
        ScoredModel model{geniex::LLMSpec{}};
        const auto result = model.prefillOnly(tokens, input);
        assert(result.prompt_tokens == 5 && result.last_chunk_tokens == 1);
        assert(result.token == 6 && !result.eos); // winner is not constrained to labels
        assert(model.firstLogits() == model.logits && model.read_calls == 2);
        assert(model.seen_tokens == tokens);
        assert((model.seen_embeds == std::vector<float>{10, 10, 101, 102, 103, 104, 11, 11, 12, 12}));
        assert(model.nPast() == tokens.size()); // sampled token is not a decoded KV entry
        assert(model.embedding().buffer.empty() && !model.positions_live);
        rejects([&] { model.prefillOnly(tokens, input); }); // no implicit prefix reuse
        assert(model.vision_calls == 1);
        model.resetKVCache();
        auto even_tokens = tokens;
        even_tokens.push_back(13);
        assert(model.prefillOnly(even_tokens, input).last_chunk_tokens == 2);
        assert(model.firstLogits() == model.logits);
    }
    for (int failure = 0; failure < 4; ++failure) {
        ScoredModel model{geniex::LLMSpec{}};
        if (failure == 0) model.vision.pop_back();
        if (failure == 1) model.fail_prefill = true;
        if (failure == 2) model.incomplete_prefill = true;
        if (failure == 3) model.fail_sample = true;
        rejects([&] { model.prefillOnly(tokens, input); });
        assert(model.nPast() == 0 && model.reset_calls == 1);
        assert(model.embedding().buffer.empty() && !model.positions_live);
        assert(model.position_clears == 1);
    }
    {
        ScoredModel model{geniex::LLMSpec{}};
        rejects([&] { model.prefillOnly({}, input); });
        rejects([&] { model.prefillOnly(std::vector<int32_t>(9, 7), input); });
        assert(model.vision_calls == 0 && model.sample_calls == 0);
    }
    {
        ScoredModel model{geniex::LLMSpec{}};
        model.duplicateEmbedding();
        rejects([&] { model.prefillOnly(tokens, input); });
        assert(model.vision_calls == 0);
    }
    {
        ScoredModel model{geniex::LLMSpec{}};
        model.removeEmbedding();
        rejects([&] { model.prefillOnly(tokens, input); });
        assert(model.vision_calls == 0);
    }
    {
        ScoredModel model{geniex::LLMSpec{}};
        model.logits.at(4) = 30;
        const auto result = model.prefillOnly(tokens, input);
        assert(result.token == 4 && result.eos);
        assert(model.firstLogits() == model.logits);
    }
    std::cout << "prefill-only lifecycle checks passed\n";
}
