// Offline streaming experiment; deliberately not accepted by the live bridge.
// Reuses the pinned model wiring and full-vocabulary first-token scorer.
#define DF_QAIRT_SCORE_WORKER_NO_MAIN
#include "qairt_score_worker.cpp"

class StreamModel : public ScoredModel {
public:
    using ScoredModel::ScoredModel;
    void setVisionEncoder(std::unique_ptr<qwen3_vl::Qwen3VLVisionEncoder> encoder) {
        encoder_ = encoder.get();
        ScoredModel::setVisionEncoder(std::move(encoder));
    }
    void setDeepstackProvider(std::unique_ptr<DeepstackInputProvider> provider) {
        deepstack_ = provider.get();
        ScoredModel::setDeepstackProvider(std::move(provider));
    }
    void clearStream() {
        features_.clear(); levels_.clear(); images_ = 0; use_stream_ = false;
        tokens_.clear(); text_.clear(); early_prefill_ms_ = 0;
        if (embedding_) embedding_->clearBuffer();
        resetKVCache();
    }
    void beginIncremental(const std::vector<int32_t>& tokens, const VLMInput& input) {
        if (n_past_ || !token_history_.empty() || tokens.empty() ||
            spec_.seq_len_prefill != 128 || tokens.size() > spec_.context_lengths.back() ||
            std::count(tokens.begin(), tokens.end(), image_token_id_) !=
                static_cast<int>(112 * input.pixel_data.image_grid_thw.size()))
            throw std::runtime_error("Invalid incremental context");
        for (auto& provider : input_providers_)
            if (auto* found = dynamic_cast<PrecomputedEmbeddingProvider*>(provider.get())) embedding_ = found;
        if (!embedding_) throw std::runtime_error("Missing incremental embedding provider");
        tokens_ = tokens; text_ = embedding_->lookupBatch(tokens);
        if (text_.size() != tokens.size() * spec_.hidden_size)
            throw std::runtime_error("Invalid incremental text embeddings");
        // Image dimensions and the complete prompt are already known. Compute
        // positions once for the original full sequence, never per-image turns.
        preparePositions(tokens, input, 0);
        GenerationConfig gen;
        gen.max_tokens = 1; gen.enable_sampling = false; gen.thinking_mode = false;
        prepareSampler(gen, tokens);
    }
    size_t earlyTokens() const { return n_past_; }
    double earlyMs() const { return early_prefill_ms_; }
    PrefillScore finishIncremental(const std::vector<int32_t>& tokens) {
        if (tokens_ != tokens || tokens_.empty() || n_past_ >= tokens.size())
            throw std::runtime_error("Incremental token identity mismatch");
        PrefillScore result;
        result.prompt_tokens = tokens.size();
        auto started = Clock::now();
        prefillChunks(std::vector<int32_t>(tokens.begin() + n_past_, tokens.end()), &result.last_chunk_tokens);
        result.prefill_ms = elapsedMs(started);
        if (n_past_ != tokens.size() || token_history_ != tokens || !result.last_chunk_tokens)
            throw std::runtime_error("Incomplete incremental prefill");
        started = Clock::now();
        result.token = sampleNextToken(0, result.last_chunk_tokens - 1);
        result.sample_ms = elapsedMs(started);
        started = Clock::now();
        embedding_->clearBuffer(); clearPositions();
        result.cleanup_ms = elapsedMs(started);
        return result;
    }
    size_t bytes() const {
        size_t count = features_.size();
        for (const auto& level : levels_) count += level.size();
        return count * sizeof(float);
    }
    void append(const PixelData& pixels) {
        if (images_ >= 3 || pixels.image_grid_thw.size() != 1 || (tokens_.empty() && n_past_))
            throw std::runtime_error("Expected one new image in a fresh bounded batch");
        auto features = encoder_->encode(pixels);
        const auto& levels = encoder_->deepstackEmbeds();
        const auto valid = [this](const std::vector<float>& data) {
            return data.size() == 112 * spec_.hidden_size &&
                std::all_of(data.begin(), data.end(), [](float x) { return std::isfinite(x); });
        };
        if (!valid(features) || levels.size() != 3 ||
            !std::all_of(levels.begin(), levels.end(), valid))
            throw std::runtime_error("Invalid compiled Reason2 visual/DeepStack features");
        if (!images_) levels_.resize(levels.size());
        features_.insert(features_.end(), features.begin(), features.end());
        for (size_t k = 0; k < levels.size(); ++k)
            levels_[k].insert(levels_[k].end(), levels[k].begin(), levels[k].end());
        ++images_;
        if (!tokens_.empty()) {
            size_t visual = 0, available = tokens_.size();
            for (size_t pos = 0; pos < tokens_.size(); ++pos) {
                if (tokens_[pos] != image_token_id_) continue;
                if (visual == images_ * 112) { available = pos; break; }
                std::copy_n(features_.data() + visual * spec_.hidden_size, spec_.hidden_size,
                            text_.data() + pos * spec_.hidden_size);
                ++visual;
            }
            embedding_->setBuffer(text_, 0);
            deepstack_->setEmbeds(levels_, spec_.hidden_size);
            // Hide only whole original AR128 blocks. Leave the final suffix for
            // explicit score(), including when all pixels are already available.
            if (available < tokens_.size()) {
                const size_t end = available / spec_.seq_len_prefill * spec_.seq_len_prefill;
                if (end > n_past_) {
                    const auto started = Clock::now();
                    prefillChunks(std::vector<int32_t>(tokens_.begin() + n_past_, tokens_.begin() + end), nullptr);
                    early_prefill_ms_ += elapsedMs(started);
                    if (n_past_ != end || token_history_ != std::vector<int32_t>(tokens_.begin(), tokens_.begin() + end))
                        throw std::runtime_error("Incremental prefix mismatch");
                }
            }
        }
    }
    void useStream() { use_stream_ = true; }
protected:
    std::vector<float> encodeVision(const PixelData& pixels) override {
        if (!use_stream_) return ScoredModel::encodeVision(pixels);
        if (!images_ || images_ != pixels.image_grid_thw.size() || !pixels.pixel_values.empty())
            throw std::runtime_error("Incomplete streamed visual input");
        deepstack_->setEmbeds(levels_, spec_.hidden_size);
        return features_;
    }
private:
    qwen3_vl::Qwen3VLVisionEncoder* encoder_ = nullptr;
    DeepstackInputProvider* deepstack_ = nullptr;
    std::vector<float> features_;
    std::vector<std::vector<float>> levels_;
    size_t images_ = 0;
    bool use_stream_ = false;
    PrecomputedEmbeddingProvider* embedding_ = nullptr;
    std::vector<int32_t> tokens_;
    std::vector<float> text_;
    double early_prefill_ms_ = 0;
};

static void keys(const json& request, const std::vector<std::string>& expected) {
    if (!request.is_object() || request.size() != expected.size())
        throw std::runtime_error("Unexpected experiment request fields");
    for (const auto& key : expected)
        if (!request.contains(key)) throw std::runtime_error("Missing experiment request field");
}

static std::string formattedPrompt(size_t count, const std::string& prompt) {
    if (count < 1 || count > 3 || prompt.empty() || prompt.size() > 8192 ||
        prompt.find("<__image__>") != std::string::npos)
        throw std::runtime_error("Invalid image count/prompt");
    std::string text = "<|im_start|>user\n";
    for (size_t i = 0; i < count; ++i) {
        if (i) text += "\n";
        text += "<__image__>";
    }
    return text + prompt + "<|im_end|>\n<|im_start|>assistant\n";
}

static json classify(StreamModel& model, Tokenizer& tokenizer,
                     const std::vector<int32_t>& tokens, const VLMInput& input, bool incremental = false) {
    const auto started = Clock::now();
    const auto early_tokens = model.earlyTokens();
    const auto early_ms = model.earlyMs();
    const auto result = incremental ? model.finishIncremental(tokens) : model.prefillOnly(tokens, input);
    const auto logits = model.firstLogits();
    if (logits.size() != 151936 ||
        !std::all_of(logits.begin(), logits.end(), [](float x) { return std::isfinite(x); }))
        throw std::runtime_error("Invalid full vocabulary output");
    const int winner = std::max_element(logits.begin(), logits.end()) - logits.begin();
    if (winner != result.token) throw std::runtime_error("Native sampler/full logit mismatch");
    const double peak = logits[winner];
    double sum = 0;
    for (float value : logits) sum += std::exp(double(value) - peak);
    const double logz = peak + std::log(sum);
    json probabilities = json::object();
    const int labels[] = {38, 32, 49};
    for (size_t i = 0; i < 3; ++i)
        probabilities[std::string(1, "GAR"[i])] = std::exp(double(logits[labels[i]]) - logz);
    return {{"raw_top_id", winner}, {"raw_top_token", tokenizer.decode({winner}, false)},
            {"label_probabilities", probabilities}, {"vocab_size", logits.size()},
            {"generated_tokens", 1}, {"prompt_tokens", tokens.size()},
            {"visual_tokens", 112 * input.pixel_data.image_grid_thw.size()},
            {"scoring_method", "gar-full-vocabulary-softmax-v1"},
            {"execution", incremental ? "offline-stream-prefill-v1" : "offline-stream-vision-v1"}, {"latency_ms", elapsedMs(started)},
            {"early_prefill_tokens", early_tokens}, {"early_prefill_ms", early_ms},
            {"profile", {{"embeddings_ms", result.embeddings_ms}, {"vision_ms", result.vision_ms},
                {"positions_ms", result.positions_ms}, {"prefill_ms", result.prefill_ms},
                {"sample_ms", result.sample_ms}, {"cleanup_ms", result.cleanup_ms}}}};
}

int main(int argc, char** argv) {
    try {
        const bool incremental = argc == 4 && std::string(argv[3]) == "--incremental";
        if (argc != 3 && !incremental)
            throw std::runtime_error("Usage: qairt_stream_worker BUNDLE QAIRT_LIB_DIRECTORY [--incremental]");
        const fs::path bundle = fs::canonical(argv[1]), libs = fs::canonical(argv[2]);
        QnnRuntimeConfig runtime;
        runtime.backend_path = (libs / "libQnnHtp.so").string();
        runtime.system_lib_path = (libs / "libQnnSystem.so").string();
        runtime.extensions_path = (libs / "libQnnHtpNetRunExtensions.so").string();
        VLMConfig config;
        for (int i = 1; i <= 4; ++i)
            config.llm_config.model_paths.push_back((bundle / ("part" + std::to_string(i) + "_of_4.bin")).string());
        config.llm_config.tokenizer_path = (bundle / "tokenizer.json").string();
        config.llm_config.embedding_path = (bundle / "embedding_weights.raw").string();
        config.llm_config.htp_config_path = (bundle / "htp_backend_ext_config.json").string();
        config.vision_config.model_paths = {(bundle / "vision_encoder.bin").string()};
        config.vision_config.htp_config_path = config.llm_config.htp_config_path;
        auto model = makeScored<StreamModel>(runtime, config);
        const auto metadata = parseQAIRTMetadata(bundle);
        const auto& vp = *metadata.vision_preprocessing;
        qwen2vl::Qwen2VLConfig pc;
        pc.fixed_height = vp.image_height; pc.fixed_width = vp.image_width;
        pc.patch_size = vp.patch_size; pc.temporal_patch_size = vp.temporal_patch_size;
        pc.merge_size = vp.spatial_merge_size; pc.image_mean = vp.normalize_mean; pc.image_std = vp.normalize_std;
        dark_factory::FixedProcessor processor(pc);
        auto native = qwen2vl::Qwen2VLProcessor::create(config.llm_config.tokenizer_path, pc);
        auto& tokenizer = native->tokenizer();
        if (tokenizer.encode("G", false) != std::vector<int32_t>{38} ||
            tokenizer.encode("A", false) != std::vector<int32_t>{32} ||
            tokenizer.encode("R", false) != std::vector<int32_t>{49})
            throw std::runtime_error("Unexpected label tokens");
        std::cout << json({{"ready", true}, {"schema", "automotive-qairt-score-worker-v1"},
            {"label_ids", {38, 32, 49}}, {"execution", incremental ? "offline-stream-prefill-v1" : "offline-stream-vision-v1"}}).dump() << std::endl;
        std::string batch, prompt;
        size_t expected = 0, received = 0;
        Clock::time_point begun;
        auto clear = [&]() { batch.clear(); prompt.clear(); expected = received = 0; model->clearStream(); };
        std::string line;
        while (std::getline(std::cin, line)) {
            json request, reply;
            try {
                const auto started = Clock::now();
                if (line.size() > 65536) throw std::runtime_error("Request too large");
                request = json::parse(line);
                if (!request.is_object() || !request.contains("request_id") ||
                    !request["request_id"].is_string() || request["request_id"].get<std::string>().empty())
                    throw std::runtime_error("Expected request identity");
                const auto op = request.at("op").get<std::string>();
                if (op == "begin") {
                    clear();
                    keys(request, {"request_id", "op", "batch_id", "prompt", "image_count"});
                    batch = request.at("batch_id").get<std::string>();
                    prompt = request.at("prompt").get<std::string>();
                    if (!request.at("image_count").is_number_unsigned())
                        throw std::runtime_error("Invalid image count type");
                    expected = request.at("image_count").get<size_t>();
                    if (batch.empty() || batch.size() > 128 || expected < 2 || expected > 3)
                        throw std::runtime_error("Invalid bounded batch identity/count");
                    formattedPrompt(expected, prompt);
                    begun = Clock::now();
                    if (incremental) {
                        VLMInput input;
                        input.pixel_data.image_grid_thw.assign(expected, {1, 16, 28});
                        model->beginIncremental(processor.tokens(tokenizer, formattedPrompt(expected, prompt), expected), input);
                    }
                    reply = {{"prepared", false}, {"batch_id", batch}, {"image_count", expected}};
                } else if (op == "baseline") {
                    clear();
                    keys(request, {"request_id", "op", "images", "prompt"});
                    const auto images = request.at("images").get<std::vector<std::string>>();
                    const auto text = formattedPrompt(images.size(), request.at("prompt").get<std::string>());
                    VLMInput input;
                    processor.pixels(images, input.pixel_data.pixel_values);
                    input.pixel_data.image_grid_thw.assign(images.size(), {1, 16, 28});
                    reply = classify(*model, tokenizer, processor.tokens(tokenizer, text, images.size()), input);
                    clear();
                } else {
                    if (batch.empty() || request.at("batch_id").get<std::string>() != batch || elapsedMs(begun) >= 8000)
                        throw std::runtime_error("Unknown or expired stream batch");
                    if (op == "append") {
                        keys(request, {"request_id", "op", "batch_id", "index", "image"});
                        if (!request.at("index").is_number_unsigned() ||
                            request.at("index").get<size_t>() != received || received >= expected)
                            throw std::runtime_error("Out-of-order/duplicate image");
                        VLMInput input;
                        processor.pixels({request.at("image").get<std::string>()}, input.pixel_data.pixel_values);
                        input.pixel_data.image_grid_thw = {{1, 16, 28}};
                        model->append(input.pixel_data); ++received;
                        reply = {{"prepared", true}, {"batch_id", batch}, {"received", received},
                                 {"feature_bytes", model->bytes()}, {"early_prefill_tokens", model->earlyTokens()},
                                 {"early_prefill_ms", model->earlyMs()}};
                    } else if (op == "score") {
                        keys(request, {"request_id", "op", "batch_id"});
                        if (received != expected) throw std::runtime_error("Incomplete stream batch");
                        VLMInput input;
                        input.pixel_data.image_grid_thw.assign(expected, {1, 16, 28});
                        const auto tokens = processor.tokens(tokenizer, formattedPrompt(expected, prompt), expected);
                        model->useStream();
                        reply = classify(*model, tokenizer, tokens, input, incremental);
                        if (elapsedMs(begun) >= 8000) throw std::runtime_error("Stream result expired");
                        reply["batch_id"] = batch;
                        reply["batch_elapsed_ms"] = elapsedMs(begun);
                        clear();
                    } else if (op == "cancel") {
                        keys(request, {"request_id", "op", "batch_id"});
                        clear(); reply = {{"cancelled", true}};
                    } else throw std::runtime_error("Unknown stream operation");
                }
                reply["operation_ms"] = elapsedMs(started);
            } catch (const std::exception& error) {
                clear(); reply = {{"error", error.what()}};
            }
            reply["request_id"] = request.is_object() && request.contains("request_id") && request["request_id"].is_string()
                ? request["request_id"] : json("");
            std::cout << reply.dump() << std::endl;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
