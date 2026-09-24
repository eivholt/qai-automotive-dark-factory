// Candidate QAIRT 2.47 / Genie worker, not a validated model deployment.
// Native JSON-lines transport. The caller owns native chat-template rendering.
// Build separately from the Reason2 GenieX/QAIRT 2.45 process; never mix libraries.
#include "genie_single_token_scoring.h"
#include <Genie/GenieDialog.h>
#include <Genie/GenieLog.h>
#include <Genie/GenieSampler.h>
#include <Genie/GenieTokenizer.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <link.h>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>

using json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr size_t MaxLine = 1024 * 1024, MaxText = 128 * 1024;
constexpr const char* Schema = "automotive-genie-single-token-worker-v1";
constexpr const char* SamplerName = "dark_factory_full_vocab_argmax_v1";

static void check(Genie_Status_t status, const char* operation) {
    if (status != GENIE_STATUS_SUCCESS)
        throw std::runtime_error(std::string(operation) + " returned Genie status " + std::to_string(status));
}
static double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
static std::string hash_stream(std::istream& in) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA256 initialization failed");
    char block[65536];
    while (in) {
        in.read(block, sizeof(block));
        if (EVP_DigestUpdate(ctx.get(), block, size_t(in.gcount())) != 1)
            throw std::runtime_error("SHA256 update failed");
    }
    if (!in.eof()) throw std::runtime_error("SHA256 input read failed");
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &length) != 1 || length != 32)
        throw std::runtime_error("SHA256 finalization failed");
    std::ostringstream out;
    for (unsigned i = 0; i < length; ++i) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(digest[i]);
    return out.str();
}
static std::string hash_text(const std::string& text) {
    std::istringstream in(text); return hash_stream(in);
}
static json file_identity(const fs::path& path) {
    auto real = fs::canonical(path);
    std::ifstream in(real, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + real.string());
    return {{"path", real.string()}, {"sha256", hash_stream(in)}, {"bytes", fs::file_size(real)}};
}
static std::string bounded_text(const json& request, const char* key) {
    const auto& value = request.at(key);
    if (!value.is_string()) throw std::invalid_argument(std::string(key) + " must be a string");
    auto text = value.get<std::string>();
    if (text.size() > MaxText || text.find('\0') != std::string::npos)
        throw std::invalid_argument("Text exceeds limit or contains NUL");
    return text;
}
static void no_selection_modifiers(const json& value) {
    if (value.is_object()) for (const auto& item : value.items()) {
        std::string key = item.key();
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        const bool modifier = key.find("lora") != std::string::npos || key.find("grammar") != std::string::npos;
        if (modifier && !item.value().is_null() &&
            item.value() != false && item.value() != 0 && !item.value().empty())
            throw std::invalid_argument("Controller config must not enable LoRA or grammar");
        no_selection_modifiers(item.value());
    } else if (value.is_array()) for (const auto& item : value) no_selection_modifiers(item);
}
static void allocate(const size_t size, const char** data) { *data = static_cast<char*>(std::malloc(size)); }
static void log_message(GenieLog_Handle_t, const char* format, GenieLog_Level_t, uint64_t, va_list args) {
    std::vfprintf(stderr, format, args); std::fputc('\n', stderr);
}
static int collect_libraries(dl_phdr_info* info, size_t, void* data) {
    std::string name = info->dlpi_name;
    if (name.find("libGenie.so") != std::string::npos || name.find("libQnn") != std::string::npos)
        static_cast<std::set<std::string>*>(data)->insert(name);
    return 0;
}

class Worker {
    GenieDialogConfig_Handle_t config_ = nullptr;
    GenieDialog_Handle_t dialog_ = nullptr;
    GenieTokenizer_Handle_t tokenizer_ = nullptr; // Owned by dialog.
    GenieLog_Handle_t logger_ = nullptr;
    uint32_t context_ = 0, vocabulary_ = 0;
    bool prefix_rewind_api_ = false;
    json provenance_, action_ids_ = json::object();
    bool active_ = false;
    size_t callbacks_ = 0;
    std::string callback_error_;
    dark_factory::Scores scores_;
    std::vector<uint32_t> emitted_;
    std::set<uint32_t> eos_ids_;
    bool end_received_ = false;
    std::vector<uint32_t> previous_input_;
    uint32_t previous_winner_ = 0;
    Clock::time_point started_;
    double ttft_ = 0;

    static void sample(uint32_t bytes, const void* logits, uint32_t count,
                       int32_t* tokens, const void* opaque) noexcept {
        auto& self = *static_cast<Worker*>(const_cast<void*>(opaque));
        // Do not unwind an exception through the C runtime. Any failure invalidates
        // this entire response, including the placeholder token required by its ABI.
        try {
            if (!self.active_ || !tokens || ++self.callbacks_ != 1)
                throw std::runtime_error("Unexpected sampler callback");
            self.scores_ = dark_factory::score_logits(logits, bytes, count, self.vocabulary_);
            tokens[0] = int32_t(self.scores_.winner);
            self.ttft_ = elapsed(self.started_);
        } catch (const std::exception& e) {
            self.callback_error_ = e.what();
            if (tokens && count) tokens[0] = 0;
        }
    }
    static void emitted(const uint32_t* tokens, uint32_t count,
                        GenieDialog_SentenceCode_t code, const void* opaque) noexcept {
        auto& self = *static_cast<Worker*>(const_cast<void*>(opaque));
        try {
            if (code == GENIE_DIALOG_SENTENCE_ABORT)
                throw std::runtime_error("Genie aborted the token query");
            if (code == GENIE_DIALOG_SENTENCE_END) self.end_received_ = true;
            if (count && (!tokens || count > 1 || self.emitted_.size() + count > 1))
                throw std::runtime_error("Genie emitted more than one token");
            if (count) self.emitted_.insert(self.emitted_.end(), tokens, tokens + count);
        } catch (const std::exception& e) { self.callback_error_ = e.what(); }
    }
    std::vector<uint32_t> token_array(const json& values) const {
        if (!values.is_array() || values.size() > context_)
            throw std::invalid_argument("Invalid token-array size");
        std::vector<uint32_t> result;
        for (const auto& value : values) {
            if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<uint64_t>() >= vocabulary_)
                throw std::invalid_argument("Token outside configured vocabulary");
            result.push_back(value.get<uint32_t>());
        }
        return result;
    }
    void free_handles() noexcept {
        if (dialog_) GenieDialog_free(dialog_);
        if (config_) GenieDialogConfig_free(config_);
        if (logger_) GenieLog_free(logger_);
        dialog_ = nullptr; config_ = nullptr; logger_ = nullptr;
    }
    uint32_t occupancy() const {
        Genie_DataType_t type{}; Genie_Value_t value{};
        check(GenieDialog_getValue(dialog_, GENIE_DIALOG_PARAM_CONTEXT_OCCUPANCY, nullptr, &type, &value),
              "read context occupancy");
        if (type != GENIE_DATATYPE_UINT_32) throw std::runtime_error("Invalid context occupancy type");
        return value.uint32Value;
    }
public:
    Worker(const fs::path& config_path, const fs::path& tokenizer_path, const fs::path& libs) {
        try {
            const auto absolute_config = fs::canonical(config_path);
            const auto absolute_tokenizer = fs::canonical(tokenizer_path);
            const auto absolute_libs = fs::canonical(libs);
            fs::current_path(absolute_config.parent_path());
            if (fs::file_size(absolute_config) > MaxLine) throw std::invalid_argument("Config is too large");
            std::ifstream input(absolute_config);
            json configuration = json::parse(input);
            no_selection_modifiers(configuration);
            auto& d = configuration.at("dialog");
            if (d.at("type") != "basic" || d.at("engine").at("backend").at("type") != "QnnHtp" ||
                d.at("engine").at("model").at("type") != "binary")
                throw std::invalid_argument("Requires a basic, binary QnnHtp dialog");
            if (fs::canonical(d.at("tokenizer").at("path").get<std::string>()) != absolute_tokenizer)
                throw std::invalid_argument("Explicit tokenizer differs from the bundle config");
            context_ = d.at("context").at("size").get<uint32_t>();
            vocabulary_ = d.at("context").at("n-vocab").get<uint32_t>();
            if (context_ < 2 || context_ > 32768 || vocabulary_ < 13 || vocabulary_ > 1048576)
                throw std::invalid_argument("Unsupported context/vocabulary bound");
            // Match Genie's context EOS set, including its legacy EOT alias.
            // Only explicitly configured terminal IDs can explain a zero-token END.
            auto terminal = [&](const json& token) {
                if (!token.is_number_integer() ||
                    (token.is_number_unsigned() && token.get<uint64_t>() >= vocabulary_) ||
                    token.get<int64_t>() < -1 ||
                    token.get<int64_t>() >= int64_t(vocabulary_))
                    throw std::invalid_argument("Invalid configured EOS token");
                if (token.get<int64_t>() >= 0) eos_ids_.insert(token.get<uint32_t>());
            };
            if (d["context"].contains("eos-token")) {
                const auto& eos = d["context"]["eos-token"];
                if (eos.is_array()) { for (const auto& token : eos) terminal(token); }
                else terminal(eos);
            }
            if (d["context"].contains("eot-token")) terminal(d["context"]["eot-token"]);
            provenance_ = {{"config", file_identity(absolute_config)},
                {"tokenizer", file_identity(absolute_tokenizer)}, {"contexts", json::array()},
                {"backend", "QnnHtp"}, {"compiled_context_tokens", context_},
                {"vocabulary_size", vocabulary_}, {"source_weights", "not established by worker; use export provenance"},
                {"LD_LIBRARY_PATH", std::getenv("LD_LIBRARY_PATH") ? std::getenv("LD_LIBRARY_PATH") : ""},
                {"ADSP_LIBRARY_PATH", std::getenv("ADSP_LIBRARY_PATH") ? std::getenv("ADSP_LIBRARY_PATH") : ""}};
            const auto& bins = d.at("engine").at("model").at("binary").at("ctx-bins");
            if (!bins.is_array() || bins.empty() || bins.size() > 32)
                throw std::invalid_argument("Invalid compiled context list");
            for (const auto& file : bins) provenance_["contexts"].push_back(file_identity(file.get<std::string>()));
            if (d.at("engine").at("backend").contains("extensions"))
                provenance_["backend_extensions"] = file_identity(d["engine"]["backend"]["extensions"].get<std::string>());
            // No change to compiled graphs, context, tokenizer, weights, or prompts.
            d["sampler"] = {{"version", 1}, {"type", "custom"}, {"callback-name", SamplerName}};
            d["max-num-tokens"] = 1;
            const auto effective = configuration.dump();
            provenance_["effective_config_sha256"] = hash_text(effective);
            provenance_["genie_api_version"] = std::to_string(Genie_getApiMajorVersion()) + "." +
                std::to_string(Genie_getApiMinorVersion()) + "." + std::to_string(Genie_getApiPatchVersion());
            // QAIRT 2.50.40's Genie 1.20.0 forwards the token callback for REWIND.
            // Do not assume that contract for older or unreviewed future APIs.
            prefix_rewind_api_ = Genie_getApiMajorVersion() == 1 && Genie_getApiMinorVersion() == 20 &&
                Genie_getApiPatchVersion() == 0;
            Dl_info genie_info{};
            if (!dladdr(reinterpret_cast<void*>(&GenieDialog_create), &genie_info) || !genie_info.dli_fname ||
                fs::canonical(genie_info.dli_fname).parent_path() != absolute_libs)
                throw std::runtime_error("Loaded libGenie differs from requested QAIRT library directory");
            check(GenieSampler_registerUserDataCallback(SamplerName, sample, this), "register sampler");
            check(GenieLog_create(nullptr, log_message, GENIE_LOG_LEVEL_WARN, &logger_), "create logger");
            check(GenieDialogConfig_createFromJson(effective.c_str(), &config_), "create config");
            check(GenieDialogConfig_bindLogger(config_, logger_), "bind logger");
            check(GenieDialog_create(config_, &dialog_), "create dialog");
            check(GenieDialog_setMaxNumTokens(dialog_, 1), "set one-token limit");
            check(GenieDialog_getTokenizer(dialog_, &tokenizer_), "get tokenizer");
            std::set<uint32_t> ids;
            for (char code = 'A'; code <= 'M'; ++code) {
                const std::string letter(1, code); auto tokens = encode(letter);
                if (tokens.size() != 1 || !ids.insert(tokens.front()).second || decode(tokens) != letter)
                    throw std::runtime_error("A-M must be unique single tokens with exact round trips");
                if (eos_ids_.count(tokens.front()))
                    throw std::runtime_error("Configured EOS token overlaps an action token");
                action_ids_[letter] = tokens.front();
            }
            std::set<std::string> loaded;
            dl_iterate_phdr(collect_libraries, &loaded);
            provenance_["loaded_runtime_libraries"] = json::array();
            for (const auto& library : loaded) {
                if (fs::canonical(library).parent_path() != absolute_libs)
                    throw std::runtime_error("Mixed QAIRT host libraries: " + library);
                provenance_["loaded_runtime_libraries"].push_back(file_identity(library));
            }
            provenance_["worker"] = file_identity("/proc/self/exe");
            provenance_["identity_sha256"] = hash_text(provenance_.dump());
        } catch (...) { free_handles(); throw; }
    }
    ~Worker() { free_handles(); }
    std::vector<uint32_t> encode(const std::string& text) const {
        const int32_t* raw = nullptr; uint32_t count = 0;
        const auto status = GenieTokenizer_encode(tokenizer_, text.c_str(), allocate, &raw, &count);
        std::unique_ptr<void, decltype(&std::free)> owner(const_cast<int32_t*>(raw), std::free);
        check(status, "tokenize");
        if (count > context_ || (count && !raw)) throw std::invalid_argument("Tokenization exceeds context");
        std::vector<uint32_t> result;
        for (uint32_t i = 0; i < count; ++i) {
            if (raw[i] < 0 || uint32_t(raw[i]) >= vocabulary_) throw std::runtime_error("Tokenizer returned invalid ID");
            result.push_back(uint32_t(raw[i]));
        }
        return result;
    }
    std::string decode(const std::vector<uint32_t>& tokens) const {
        if (tokens.empty()) return "";
        std::vector<int32_t> signed_ids(tokens.begin(), tokens.end());
        const char* raw = nullptr;
        auto status = GenieTokenizer_decode(tokenizer_, signed_ids.data(), uint32_t(signed_ids.size()), allocate, &raw);
        std::unique_ptr<void, decltype(&std::free)> owner(const_cast<char*>(raw), std::free);
        check(status, "decode");
        if (!raw) throw std::runtime_error("Tokenizer returned null text");
        return std::string(raw);
    }
    json health() const {
        json modes = {"reset", "append"};
        if (prefix_rewind_api_) modes.push_back("append_rewind");
        return {{"ready", true}, {"schema", Schema}, {"candidate", true}, {"action_token_ids", action_ids_},
                {"configured_eos_token_ids", eos_ids_},
                {"provenance", provenance_}, {"cache_modes", modes},
                {"append_cache_experimental", true}, {"disk_cache_supported", false},
                {"prefix_rewind_experimental", true}, {"prefix_rewind_api_supported", prefix_rewind_api_},
                {"scoring_method", "full-vocabulary-argmax-softmax-v1"}};
    }
    json run(const json& request) {
        if (!request.is_object()) throw std::invalid_argument("Request must be an object");
        const std::string op = request.at("op").get<std::string>();
        if (op == "health") return health();
        if (op == "tokenize") {
            if (request.value("add_special", false)) throw std::invalid_argument("Tokenize requires add_special=false");
            return {{"tokens", encode(bounded_text(request, "content"))}};
        }
        if (op == "decode") return {{"content", decode(token_array(request.at("tokens")))}};
        if (op != "completion") throw std::invalid_argument("Unsupported operation");
        if (request.value("n_predict", 1) != 1 || request.value("temperature", 0.0) != 0.0 ||
            request.value("n_probs", 128) != 128 || request.value("post_sampling_probs", false) ||
            request.value("repeat_penalty", 1.0) != 1.0 || request.value("presence_penalty", 0.0) != 0.0 ||
            request.value("frequency_penalty", 0.0) != 0.0)
            throw std::invalid_argument("Only one unmodified greedy next-token score is supported");
        if (request.contains("samplers") && request["samplers"] != json::array({"temperature"}))
            throw std::invalid_argument("Sampler must be temperature-only greedy");
        for (const char* field : {"grammar", "logit_bias", "lora", "stop", "penalty_prompt"})
            if (request.contains(field) && !request[field].empty() && !request[field].is_null())
                throw std::invalid_argument(std::string("Unsupported selection modifier: ") + field);
        if (request.contains("prompt") == request.contains("tokens"))
            throw std::invalid_argument("Supply exactly one rendered prompt or token array");
        auto tokens = request.contains("tokens") ? token_array(request["tokens"]) : encode(bounded_text(request, "prompt"));
        if (tokens.empty() || tokens.size() >= context_)
            throw std::invalid_argument("Prompt plus one output token exceeds context, or prompt is empty");
        auto mode = request.value("cache_mode", std::string("reset"));
        if (mode != "reset" && mode != "append" && mode != "append_rewind")
            throw std::invalid_argument("Use reset, append or explicit experimental append_rewind");
        if (mode == "append_rewind" && !prefix_rewind_api_)
            throw std::invalid_argument("Experimental prefix rewind requires reviewed Genie API 1.20.0 / QAIRT 2.50.40");
        bool append = mode != "reset" && !previous_input_.empty() && tokens.size() > previous_input_.size() &&
            std::equal(previous_input_.begin(), previous_input_.end(), tokens.begin()) &&
            tokens[previous_input_.size()] == previous_winner_;
        bool rewind = false;
        size_t common = 0;
        std::string fallback;
        if (mode != "reset" && !append)
            fallback = "No exact previous-input prefix followed by the actual sampled token";
        if (mode == "append_rewind" && !append && !previous_input_.empty()) {
            while (common < tokens.size() && common < previous_input_.size() &&
                   tokens[common] == previous_input_[common]) ++common;
            // The native boundary path can emit a previously saved token before
            // sampling. Permit only a real divergence inside BOTH evaluated inputs:
            // never an identical prompt, a shortened prefix or the saved winner.
            rewind = common > 0 && common < previous_input_.size() && common < tokens.size();
            fallback = rewind ? "" : "Prefix rewind requires an interior divergence in both evaluated inputs";
        }
        if ((append || rewind) && occupancy() != previous_input_.size()) {
            append = rewind = false;
            fallback = "Runtime occupancy differs from the previously evaluated input";
        }
        const size_t retained = append ? previous_input_.size() : rewind ? common : 0;
        // REWIND receives the entire actual prompt; the runtime performs its own
        // prefix match. The common-prefix count is expected reuse, not a hardware
        // measurement. END append alone receives just its unevaluated suffix.
        const std::vector<uint32_t> submitted(tokens.begin() + (append ? retained : 0), tokens.end());
        started_ = Clock::now(); callbacks_ = 0; ttft_ = 0;
        callback_error_.clear(); emitted_.clear(); end_received_ = false; scores_ = {}; active_ = true;
        try {
            if (!append && !rewind) check(GenieDialog_reset(dialog_), "reset dialog");
            // END on a reset dialog sends exact supplied tokens, without Genie
            // injecting BOS/EOS or applying its text instruction template.
            // With append, the suffix starts with the previous sampled token:
            // it was emitted but never evaluated under max-num-tokens=1. END
            // inserts no previous token, BOS, EOS, or instruction tags.
            auto status = GenieDialog_tokenQuery(dialog_, submitted.data(), uint32_t(submitted.size()),
                rewind ? GENIE_DIALOG_SENTENCE_REWIND : GENIE_DIALOG_SENTENCE_END, emitted, this);
            active_ = false;
            check(status, "token query");
            if (!callback_error_.empty()) throw std::runtime_error(callback_error_);
            // Genie emits a zero-token END for an EOS winner. Preserve the actual
            // sampler result for diagnostics; never accept any other missing output.
            const bool suppressed_eos = callbacks_ == 1 && emitted_.empty() && end_received_ &&
                eos_ids_.count(scores_.winner) != 0;
            if (!end_received_ || callbacks_ != 1 || (!suppressed_eos &&
                (emitted_.size() != 1 || emitted_.front() != scores_.winner)))
                throw std::runtime_error("Emitted first token differs from the raw vocabulary winner");
            const auto occupied = occupancy();
            if (occupied != tokens.size())
                throw std::runtime_error("Runtime occupancy differs from the exact full input token count");
            const auto inference_ms = elapsed(started_);
            previous_input_ = tokens; previous_winner_ = scores_.winner;
            if (eos_ids_.count(scores_.winner)) previous_input_.clear();
            json top = json::array(), actions = json::object();
            for (const auto& score : scores_.top)
                top.push_back({{"id", score.id}, {"logprob", score.logprob}});
            for (const auto& item : action_ids_.items())
                actions[item.key()] = std::exp(scores_.logprobs.at(item.value().get<uint32_t>()));
            const std::vector<uint32_t> selected{scores_.winner};
            const auto content = decode(selected);
            return {{"content", content}, {"tokens", selected}, {"tokens_predicted", 1}, {"truncated", false},
                {"native_emitted_token_count", emitted_.size()}, {"native_eos_suppressed", suppressed_eos},
                {"native_sampler_callback_count", callbacks_},
                {"generation_settings", {{"temperature", 0}, {"samplers", {"temperature"}},
                    {"native_sampler", SamplerName}, {"post_sampling_probs", false}, {"grammar", ""},
                    {"logit_bias", json::array()}, {"lora", json::array()}, {"repeat_penalty", 1.0}}},
                {"completion_probabilities", json::array({{{"id", scores_.winner},
                    {"logprob", scores_.logprobs[scores_.winner]}, {"top_logprobs", top}}})},
                {"raw_winner_token_id", scores_.winner}, {"action_probabilities_full_vocab", actions},
                {"vocab_size", vocabulary_}, {"prompt_tokens", tokens.size()},
                {"prompt_tokens_sha256", hash_text(json(tokens).dump())},
                {"submitted_tokens", submitted.size()},
                {"submitted_tokens_sha256", hash_text(json(submitted).dump())},
                {"timings", {{"inference_ms", inference_ms}, {"ttft_ms", ttft_},
                    {"prompt_n", tokens.size()}, {"predicted_n", 1},
                    {"cache_mode", append ? "append" : rewind ? "rewind" : "reset"}, {"cache_mode_requested", mode},
                    {"cache_fallback", fallback}, {"submitted_prompt_n", submitted.size()},
                    {"retained_context_tokens", retained}, {"context_occupancy", occupied},
                    {"rewind_expected_prefix_tokens", rewind ? common : 0},
                    {"expected_fresh_prompt_n", tokens.size() - retained},
                    {"reused_tokens_measured", nullptr}}},
                {"provenance_sha256", provenance_["identity_sha256"]},
                {"scoring_method", "full-vocabulary-argmax-softmax-v1"}};
        } catch (...) {
            active_ = false; previous_input_.clear();
            GenieDialog_reset(dialog_); throw;
        }
    }
};

// Bounded reader drains an oversized line so the following request stays framed.
static bool read_line(std::string& line, bool& oversized) {
    line.clear(); oversized = false; char c;
    while (std::cin.get(c)) {
        if (c == '\n') return true;
        if (line.size() < MaxLine) line += c; else oversized = true;
    }
    return !line.empty() || oversized;
}
int main(int argc, char** argv) {
    // Protect the protocol from unsolicited stdout in vendor runtime libraries.
    const int fd = dup(STDOUT_FILENO);
    FILE* protocol = fd < 0 ? nullptr : fdopen(fd, "w");
    if (!protocol || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) return 2;
    auto send = [protocol](const json& value) {
        const auto line = value.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
        if (std::fwrite(line.data(), 1, line.size(), protocol) != line.size() || std::fflush(protocol))
            throw std::runtime_error("Protocol output failed");
    };
    try {
        if (argc != 4) throw std::invalid_argument("Usage: genie_single_token_worker CONFIG TOKENIZER QAIRT_LIB_DIRECTORY");
        Worker worker(argv[1], argv[2], argv[3]); send(worker.health());
        std::string line; bool oversized;
        while (read_line(line, oversized)) {
            json request, reply; std::string id;
            try {
                if (oversized) throw std::invalid_argument("Request exceeds 1 MiB");
                request = json::parse(line);
                if (!request.is_object() || !request.contains("request_id") || !request["request_id"].is_string())
                    throw std::invalid_argument("Expected string request_id");
                id = request["request_id"].get<std::string>();
                if (id.size() > 256) { id.clear(); throw std::invalid_argument("request_id exceeds 256 bytes"); }
                reply = worker.run(request);
            } catch (const std::exception& e) { reply = {{"error", e.what()}}; }
            reply["request_id"] = id; send(reply);
        }
        std::fclose(protocol); return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what()); std::fclose(protocol); return 1;
    }
}
