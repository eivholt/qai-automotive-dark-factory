// Automotive QAIRT experiment. Build against the pinned GenieX v0.3.17 headers
// and matching libraries. JSON-lines over stdin/stdout; no network listener.
// Factory wiring follows Qualcomm geniex-qairt-plugin f00a37cd (BSD-3-Clause).
// See qairt_score_worker.NOTICE for the retained upstream license.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include "qwen3_vl/qwen3_vl.h"

using json = qualla::json;
using namespace geniex;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct PrefillScore {
    int32_t token = -1;
    bool eos = false;
    size_t prompt_tokens = 0;
    size_t last_chunk_tokens = 0;
    double embeddings_ms = 0;
    double vision_ms = 0;
    double positions_ms = 0;
    double prefill_ms = 0;
    double sample_ms = 0;
    double cleanup_ms = 0;
};

static double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

class ScoredModel : public qwen3_vl::Qwen3VLModel {
public:
    using Qwen3VLModel::Qwen3VLModel;
    std::vector<float> firstLogits() const {
        if (!n_past_ || !spec_.seq_len_prefill) throw std::runtime_error("No prefill output");
        std::vector<float> result;
        readLastLogits(0, (n_past_ - 1) % spec_.seq_len_prefill, result);
        return result;
    }

    // Explicit offline candidate: the stock generate() restrides every KV layer
    // for decode before emitting its first token, then restrides back even when
    // our callback stops immediately. This path leaves the fresh cache in its
    // prefill layout throughout. It invokes the same vision/position providers,
    // chunked prefill and native unmasked sampler; no graph or weight changes.
    PrefillScore prefillOnly(const std::vector<int32_t>& tokens, const VLMInput& input) {
        if (n_past_ != 0 || !token_history_.empty())
            throw std::runtime_error("Prefill-only scoring requires a fresh cache");
        if (tokens.empty() || !spec_.seq_len_prefill || spec_.context_lengths.empty() ||
            tokens.size() > spec_.context_lengths.back())
            throw std::runtime_error("Invalid prefill-only context length");
        PrecomputedEmbeddingProvider* embedding = nullptr;
        for (const auto& provider : input_providers_) {
            if (auto* candidate = dynamic_cast<PrecomputedEmbeddingProvider*>(provider.get())) {
                if (embedding) throw std::runtime_error("Multiple embedding providers");
                embedding = candidate;
            }
        }
        if (!embedding) throw std::runtime_error("Missing embedding provider");

        PrefillScore result;
        result.prompt_tokens = tokens.size();
        auto start = Clock::now();
        try {
            auto text_embeds = embedding->lookupBatch(tokens);
            if (text_embeds.size() != tokens.size() * spec_.hidden_size)
                throw std::runtime_error("Invalid text embedding dimensions");
            result.embeddings_ms = elapsedMs(start);

            start = Clock::now();
            auto visual_embeds = encodeVision(input.pixel_data);
            const size_t image_tokens = std::count(tokens.begin(), tokens.end(), image_token_id_);
            if (!image_tokens || visual_embeds.size() != image_tokens * spec_.hidden_size)
                throw std::runtime_error("Invalid vision embedding dimensions");
            result.vision_ms = elapsedMs(start);

            start = Clock::now();
            maskedScatter(text_embeds, visual_embeds, tokens, image_token_id_, spec_.hidden_size);
            embedding->setBuffer(std::move(text_embeds), 0);
            preparePositions(tokens, input, 0);
            GenerationConfig gen;
            gen.max_tokens = 1; gen.enable_sampling = false; gen.thinking_mode = false;
            prepareSampler(gen, tokens);
            result.positions_ms = elapsedMs(start);

            start = Clock::now();
            prefillChunks(tokens, &result.last_chunk_tokens);
            result.prefill_ms = elapsedMs(start);
            if (n_past_ != tokens.size() || token_history_ != tokens ||
                !result.last_chunk_tokens || result.last_chunk_tokens > spec_.seq_len_prefill)
                throw std::runtime_error("Incomplete prefill-only execution");

            start = Clock::now();
            // Retain the existing independent native sampler/readback comparison.
            // Reading only G/A/R logits would change the full-vocabulary contract.
            result.token = sampleNextToken(0, result.last_chunk_tokens - 1);
            result.eos = std::find(spec_.eos_token_ids.begin(), spec_.eos_token_ids.end(), result.token)
                         != spec_.eos_token_ids.end();
            result.sample_ms = elapsedMs(start);
            start = Clock::now();
            embedding->clearBuffer();
            clearPositions();
            result.cleanup_ms = elapsedMs(start);
            return result;
        } catch (...) {
            embedding->clearBuffer();
            clearPositions();
            // A partially executed prefill must never be reused by another call.
            // The request loop also performs its normal reset before every job.
            resetKVCache();
            throw;
        }
    }
};

#ifndef DF_QAIRT_SCORE_WORKER_TEST
#include "qairt_fixed_processor.h"
// Use a real subclass to expose the existing protected readLastLogits API. No
// runtime library patch, logits masking, forced label, or second inference.
template<class Model = ScoredModel>
std::unique_ptr<Model> makeScored(const QnnRuntimeConfig& runtime, const VLMConfig& cfg) {
    auto bundle = bundleDirOf(cfg.llm_config);
    auto meta = parseQAIRTMetadata(bundle);
    auto gc = parseGenieConfig(bundle);
    if (!meta.vision_preprocessing) throw std::runtime_error("Missing vision profile");
    auto& vp = *meta.vision_preprocessing;
    if (vp.image_height != 256 || vp.image_width != 448)
        throw std::runtime_error("Expected 448x256 vision graph");
    auto vis = std::make_unique<qwen3_vl::Qwen3VLVisionEncoder>("deepstack_visual_embeds_");
    vis->setPreprocessing(vp);
    vis->setHiddenSize(meta.hidden_size);
    if (!vis->initialize(runtime, cfg.vision_config)) throw std::runtime_error("Vision initialization failed");
    auto model = std::make_unique<Model>(buildSpecSkeleton(gc));
    model->setVisionEncoder(std::move(vis));
    std::vector<int> sections;
    if (auto* rope = std::get_if<MRopeScaling>(&gc.rope_scaling)) sections = rope->mrope_section;
    if (sections.empty()) throw std::runtime_error("Missing trained MRoPE sections");
    model->setMRoPEProvider(std::make_unique<MRoPEInputProvider>(sections, gc.rope_theta, MRoPEInterleaving::STRIDE));
    model->setDeepstackProvider(std::make_unique<DeepstackInputProvider>("deepstack_visual_embeds_"));
    model->setVisionTokenIds(qwen3_vl::kVisionStartTokenId, qwen3_vl::kImageTokenId);
    model->setSpatialMergeSize(vp.spatial_merge_size);
    if (!model->initialize(runtime, cfg.llm_config)) throw std::runtime_error("Text initialization failed");
    return model;
}

#ifndef DF_QAIRT_SCORE_WORKER_NO_MAIN
int main(int argc, char** argv) {
    try {
        bool prefill_only = false, fused_processor = false;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--prefill-only" && !prefill_only) prefill_only = true;
            else if (option == "--fused-processor" && !fused_processor) fused_processor = true;
            else throw std::runtime_error("Unknown or repeated worker option: " + option);
        }
        if (argc < 3 || (fused_processor && !prefill_only))
            throw std::runtime_error("Usage: qairt_score_worker BUNDLE QAIRT_LIB_DIRECTORY [--prefill-only [--fused-processor]]");
        const std::string execution = fused_processor ? "prefill-fused-v1" :
            (prefill_only ? "prefill-only-v1" : "pipeline-callback-v1");
        fs::path bundle = fs::canonical(argv[1]), libs = fs::canonical(argv[2]);
        QnnRuntimeConfig runtime;
        if (std::getenv("DF_QAIRT_DEBUG")) {
            runtime.log_level = QNN_LOG_LEVEL_VERBOSE;
            geniex_set_log_callback([](LogLevel, const char* message) { std::cerr << message << '\n'; });
        }
        runtime.backend_path = (libs / "libQnnHtp.so").string();
        runtime.system_lib_path = (libs / "libQnnSystem.so").string();
        runtime.extensions_path = (libs / "libQnnHtpNetRunExtensions.so").string();
        VLMConfig cfg;
        for (int i = 1; i <= 4; ++i)
            cfg.llm_config.model_paths.push_back((bundle / ("part" + std::to_string(i) + "_of_4.bin")).string());
        cfg.llm_config.tokenizer_path = (bundle / "tokenizer.json").string();
        cfg.llm_config.embedding_path = (bundle / "embedding_weights.raw").string();
        cfg.llm_config.htp_config_path = (bundle / "htp_backend_ext_config.json").string();
        cfg.vision_config.model_paths = {(bundle / "vision_encoder.bin").string()};
        cfg.vision_config.htp_config_path = cfg.llm_config.htp_config_path;
        auto model = makeScored(runtime, cfg);
        ScoredModel* scores = model.get();
        auto meta = parseQAIRTMetadata(bundle);
        auto& vp = *meta.vision_preprocessing;
        qwen2vl::Qwen2VLConfig pc;
        pc.fixed_height = vp.image_height; pc.fixed_width = vp.image_width;
        pc.patch_size = vp.patch_size; pc.temporal_patch_size = vp.temporal_patch_size;
        pc.merge_size = vp.spatial_merge_size;
        pc.image_mean = vp.normalize_mean; pc.image_std = vp.normalize_std;
        std::unique_ptr<dark_factory::FixedProcessor> fixed_processor;
        if (fused_processor) fixed_processor = std::make_unique<dark_factory::FixedProcessor>(pc);
        auto processor = qwen2vl::Qwen2VLProcessor::create(cfg.llm_config.tokenizer_path, pc);
        auto& tokenizer = processor->tokenizer();
        std::vector<int32_t> labels;
        for (auto letter : {"G", "A", "R"}) {
            auto ids = tokenizer.encode(letter, false);
            if (ids.size() != 1) throw std::runtime_error("Class label is not one token");
            labels.push_back(ids[0]);
        }
        VLMPipeline pipe;
        auto* native_processor = processor.get();  // owned by pipe after create()
        if (!pipe.create(std::move(model), std::move(processor), tokenizer)) throw std::runtime_error("Pipeline failed");
        std::cout << json({{"ready", true}, {"schema", "automotive-qairt-score-worker-v1"}, {"label_ids", labels},
                          {"execution", execution}}).dump() << std::endl;
        std::string line;
        while (std::getline(std::cin, line)) {
            json request, reply;
            try {
                if (line.size() > 65536) throw std::runtime_error("Request too large");
                request = json::parse(line);
                if (!request.is_object() || !request.contains("request_id") || !request["request_id"].is_string())
                    throw std::runtime_error("Expected a string request_id");
                auto images = request.at("images").get<std::vector<std::string>>();
                if (images.empty() || images.size() > 3) throw std::runtime_error("Expected one to three images");
                auto prompt = request.at("prompt").get<std::string>();
                if (prompt.size() > 8192) throw std::runtime_error("Prompt too large");
                // Preserve the trained user-only conversation and newline-separated
                // independent camera markers. No default system prompt is injected.
                std::string formatted = "<|im_start|>user\n";
                for (size_t i = 0; i < images.size(); ++i) {
                    if (i) formatted += "\n";
                    formatted += "<__image__>";
                }
                formatted += prompt + "<|im_end|>\n<|im_start|>assistant\n";
                GenerationConfig gen;
                gen.max_tokens = 1; gen.enable_sampling = false; gen.thinking_mode = false;
                auto reset_start = Clock::now();
                pipe.reset();
                const double reset_ms = elapsedMs(reset_start);
                auto start = Clock::now();
                // Returning false stops immediately after the first sampled token;
                // the stock generate loop would otherwise perform an unused decode.
                GenerateResult result;
                int32_t sampled_token_id = -1;
                json profile = {{"reset_ms", reset_ms}};
                if (prefill_only) {
                    auto prepare_start = Clock::now();
                    // Identical to VLMPipeline::generate's processor conversion:
                    // preserve all image markers, token order and normalized pixels.
                    VLMInput input;
                    std::vector<int32_t> tokens;
                    if (fixed_processor) {
                        fixed_processor->pixels(images, input.pixel_data.pixel_values);
                        tokens = fixed_processor->tokens(tokenizer, formatted, images.size());
                        for (size_t i = 0; i < images.size(); ++i)
                            input.pixel_data.image_grid_thw.push_back({1, 16, 28});
                    } else {
                        auto bf = native_processor->process(formatted, images);
                        input.pixel_data.pixel_values.assign(bf.pixel_values.cbegin(), bf.pixel_values.cend());
                        if (bf.image_grid_thw.dimension() != 2 || bf.image_grid_thw.shape()[0] != images.size() ||
                            bf.image_grid_thw.shape()[1] != 3)
                            throw std::runtime_error("Invalid processed image grid");
                        for (size_t i = 0; i < images.size(); ++i) {
                            input.pixel_data.image_grid_thw.push_back({static_cast<int32_t>(bf.image_grid_thw(i, 0)),
                                static_cast<int32_t>(bf.image_grid_thw(i, 1)), static_cast<int32_t>(bf.image_grid_thw(i, 2))});
                        }
                        tokens.assign(bf.input_ids.cbegin(), bf.input_ids.cend());
                    }
                    profile["processor_ms"] = elapsedMs(prepare_start);
                    auto inference_start = Clock::now();
                    const auto scored = scores->prefillOnly(tokens, input);
                    sampled_token_id = scored.token;
                    result.full_text = scored.eos ? "" : tokenizer.decode_token(scored.token);
                    result.generated_tokens = 1;
                    result.prompt_tokens = scored.prompt_tokens;
                    result.ttft_ms = elapsedMs(inference_start) - scored.cleanup_ms;
                    result.stop_reason = scored.eos ? "eos" : "user";
                    profile.update({{"embeddings_ms", scored.embeddings_ms}, {"vision_ms", scored.vision_ms},
                        {"positions_ms", scored.positions_ms}, {"prefill_ms", scored.prefill_ms},
                        {"sample_ms", scored.sample_ms}, {"cleanup_ms", scored.cleanup_ms},
                        {"last_chunk_tokens", scored.last_chunk_tokens}, {"sampled_token_id", sampled_token_id}});
                } else {
                    result = pipe.generate(formatted, images, gen, [](const char*) { return false; });
                }
                if (result.generated_tokens != 1) throw std::runtime_error("Expected exactly one generated token");
                if (result.stop_reason == "error" || result.stop_reason == "context_length")
                    throw std::runtime_error("Invalid prefill: " + result.stop_reason);
                auto scoring_start = Clock::now();
                auto logits = scores->firstLogits();
                if (logits.empty() || !std::all_of(logits.begin(), logits.end(), [](float x) { return std::isfinite(x); }))
                    throw std::runtime_error("Invalid full-vocabulary logits");
                double peak = *std::max_element(logits.begin(), logits.end()), mass = 0;
                for (float x : logits) mass += std::exp(double(x) - peak);
                double logz = peak + std::log(mass);
                json probs = json::object();
                for (size_t i = 0; i < labels.size(); ++i)
                    probs[std::string(1, "GAR"[i])] = std::exp(double(logits.at(labels[i])) - logz);
                auto winner = int(std::max_element(logits.begin(), logits.end()) - logits.begin());
                if (prefill_only && winner != sampled_token_id)
                    throw std::runtime_error("Logit row does not match native first token ID");
                auto top_text = tokenizer.decode({winner}, false);
                if (!result.full_text.empty() && top_text != result.full_text)
                    throw std::runtime_error("Logit row does not match emitted first token");
                profile["scoring_ms"] = elapsedMs(scoring_start);
                profile["worker_with_reset_ms"] = elapsedMs(reset_start);
                reply = {{"raw_top_token", top_text}, {"raw_top_id", winner}, {"emitted_text", result.full_text},
                         {"label_probabilities", probs}, {"vocab_size", logits.size()},
                         {"prompt_tokens", result.prompt_tokens}, {"visual_tokens", 112 * images.size()},
                         {"generated_tokens", result.generated_tokens}, {"ttft_ms", result.ttft_ms},
                         {"latency_ms", std::chrono::duration<double, std::milli>(Clock::now() - start).count()},
                         {"scoring_method", "gar-full-vocabulary-softmax-v1"},
                         {"execution", execution}, {"profile", profile}};
            } catch (const std::exception& e) {
                reply = {{"error", e.what()}};
            }
            reply["request_id"] = request.is_object() && request.contains("request_id") && request["request_id"].is_string()
                ? request["request_id"] : json("");
            std::cout << reply.dump() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
#endif
#endif
