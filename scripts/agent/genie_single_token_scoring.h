// Pure scoring for the experimental Genie worker. No action filtering or sampling.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace dark_factory {
struct TokenScore { uint32_t id; double logprob; };
struct Scores {
    uint32_t winner = 0;
    std::vector<double> logprobs;
    std::vector<TokenScore> top;
};

inline Scores score_logits(const void* buffer, size_t bytes, uint32_t requested_tokens,
                           size_t vocabulary, size_t count = 128) {
    if (!buffer || requested_tokens != 1 || vocabulary < 13 || vocabulary > 1048576 ||
        bytes != vocabulary * sizeof(float) || !count || count > 128)
        throw std::invalid_argument("Invalid one-token full-vocabulary logits buffer");
    // The Genie 2.47 custom callback supplies bytes, not a float element count.
    std::vector<float> logits(vocabulary);
    std::memcpy(logits.data(), buffer, bytes);
    if (!std::all_of(logits.begin(), logits.end(), [](float x) { return std::isfinite(x); }))
        throw std::invalid_argument("Non-finite full-vocabulary logit");
    Scores out;
    out.winner = uint32_t(std::max_element(logits.begin(), logits.end()) - logits.begin());
    const double peak = logits[out.winner];
    double mass = 0;
    for (float x : logits) mass += std::exp(double(x) - peak);
    const double logmass = std::log(mass);
    out.logprobs.reserve(vocabulary);
    for (float x : logits) out.logprobs.push_back((double(x) - peak) - logmass);
    std::vector<uint32_t> order(vocabulary);
    std::iota(order.begin(), order.end(), 0);
    const size_t n = std::min(count, vocabulary);
    std::partial_sort(order.begin(), order.begin() + n, order.end(), [&](auto a, auto b) {
        return logits[a] != logits[b] ? logits[a] > logits[b] : a < b;
    });
    for (size_t i = 0; i < n; ++i) out.top.push_back({order[i], out.logprobs[order[i]]});
    return out;
}
} // namespace dark_factory
