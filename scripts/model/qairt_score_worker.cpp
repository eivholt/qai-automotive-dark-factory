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

class ScoredModel : public qwen3_vl::Qwen3VLModel {
public:
    using Qwen3VLModel::Qwen3VLModel;
    std::vector<float> firstLogits() const {
        if (!n_past_ || !spec_.seq_len_prefill) throw std::runtime_error("No prefill output");
        std::vector<float> result;
        readLastLogits(0, (n_past_ - 1) % spec_.seq_len_prefill, result);
        return result;
    }
};

// Use a real subclass to expose the existing protected readLastLogits API. No
// runtime library patch, logits masking, forced label, or second inference.
std::unique_ptr<ScoredModel> makeScored(const QnnRuntimeConfig& runtime, const VLMConfig& cfg) {
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
    auto model = std::make_unique<ScoredModel>(buildSpecSkeleton(gc));
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

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: qairt_score_worker BUNDLE QAIRT_LIB_DIRECTORY");
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
        auto processor = qwen2vl::Qwen2VLProcessor::create(cfg.llm_config.tokenizer_path, pc);
        auto& tokenizer = processor->tokenizer();
        std::vector<int32_t> labels;
        for (auto letter : {"G", "A", "R"}) {
            auto ids = tokenizer.encode(letter, false);
            if (ids.size() != 1) throw std::runtime_error("Class label is not one token");
            labels.push_back(ids[0]);
        }
        VLMPipeline pipe;
        if (!pipe.create(std::move(model), std::move(processor), tokenizer)) throw std::runtime_error("Pipeline failed");
        std::cout << json({{"ready", true}, {"schema", "automotive-qairt-score-worker-v1"}, {"label_ids", labels}}).dump() << std::endl;
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
                pipe.reset();
                auto start = Clock::now();
                // Returning false stops immediately after the first sampled token;
                // the stock generate loop would otherwise perform an unused decode.
                auto result = pipe.generate(formatted, images, gen, [](const char*) { return false; });
                if (result.generated_tokens != 1) throw std::runtime_error("Expected exactly one generated token");
                if (result.stop_reason == "error" || result.stop_reason == "context_length")
                    throw std::runtime_error("Invalid prefill: " + result.stop_reason);
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
                auto top_text = tokenizer.decode({winner}, false);
                if (!result.full_text.empty() && top_text != result.full_text)
                    throw std::runtime_error("Logit row does not match emitted first token");
                reply = {{"raw_top_token", top_text}, {"raw_top_id", winner}, {"emitted_text", result.full_text},
                         {"label_probabilities", probs}, {"vocab_size", logits.size()},
                         {"prompt_tokens", result.prompt_tokens}, {"visual_tokens", 112 * images.size()},
                         {"generated_tokens", result.generated_tokens}, {"ttft_ms", result.ttft_ms},
                         {"latency_ms", std::chrono::duration<double, std::milli>(Clock::now() - start).count()},
                         {"scoring_method", "gar-full-vocabulary-softmax-v1"}};
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
