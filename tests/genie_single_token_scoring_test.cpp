#include "../scripts/agent/genie_single_token_scoring.h"
#include <cassert>
#include <limits>

int main() {
    std::vector<float> logits(256, -8);
    // The global winner deliberately lies outside the thirteen action IDs.
    logits[230] = 4; logits[3] = 3;
    auto score = dark_factory::score_logits(logits.data(), logits.size() * sizeof(float), 1, logits.size());
    assert(score.winner == 230 && score.top.front().id == 230 && score.top.size() == 128);
    double mass = 0; for (double p : score.logprobs) mass += std::exp(p);
    assert(std::abs(mass - 1) < 1e-12);
    for (float& x : logits) x += 10000;
    auto shifted = dark_factory::score_logits(logits.data(), logits.size() * sizeof(float), 1, logits.size());
    assert(shifted.winner == score.winner);
    for (size_t i = 0; i < score.logprobs.size(); ++i)
        assert(std::abs(shifted.logprobs[i] - score.logprobs[i]) < 1e-12);
    logits[2] = logits[230];
    auto tied = dark_factory::score_logits(logits.data(), logits.size() * sizeof(float), 1, logits.size());
    assert(tied.winner == 2 && tied.top[0].id == 2 && tied.top[1].id == 230);
    int rejected = 0;
    try { dark_factory::score_logits(logits.data(), logits.size(), 1, logits.size()); }
    catch (const std::invalid_argument&) { ++rejected; }
    try { dark_factory::score_logits(logits.data(), logits.size() * sizeof(float), 2, logits.size()); }
    catch (const std::invalid_argument&) { ++rejected; }
    logits[14] = std::numeric_limits<float>::quiet_NaN();
    try { dark_factory::score_logits(logits.data(), logits.size() * sizeof(float), 1, logits.size()); }
    catch (const std::invalid_argument&) { ++rejected; }
    assert(rejected == 3);
}
