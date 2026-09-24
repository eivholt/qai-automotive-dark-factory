// Compare actual GGML rotary output against Genie 2.50.40's YaRN formula.
// Build instructions and model scope: docs/ministral_qairt_export.md.
#include "ggml.h"
#include "ggml-cpu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

int main() {
    constexpr int dimensions = 128, positions = 4096;
    constexpr double pi = 3.14159265358979323846;
    ggml_init_params params{64u * 1024u * 1024u, nullptr, false};
    ggml_context *context = ggml_init(params);
    if (!context) return 2;
    auto input = ggml_new_tensor_3d(context, GGML_TYPE_F32, dimensions, 1, positions);
    auto position = ggml_new_tensor_1d(context, GGML_TYPE_I32, positions);
    auto values = static_cast<float *>(input->data);
    auto indices = static_cast<int32_t *>(position->data);
    for (int t = 0; t < positions; ++t) {
        indices[t] = t;
        for (int j = 0; j < dimensions; ++j) values[t * dimensions + j] = j % 2 ? 0.f : 1.f;
    }
    const float factor = 1.f / (1.f + .1f * std::log(16.f));
    auto output = ggml_rope_ext(context, input, position, nullptr, dimensions, 0,
                               16384, 1000000.f, 1.f / 16.f, 1.f, factor, 32.f, 1.f);
    auto graph = ggml_new_graph(context);
    ggml_build_forward_expand(graph, output);
    if (ggml_graph_compute_with_ctx(context, graph, 4) != GGML_STATUS_SUCCESS) {
        ggml_free(context);
        return 2;
    }
    const double low = std::max(0., std::floor(dimensions * std::log(16384. / (32. * 2. * pi)) /
                                             (2. * std::log(1000000.))));
    const double high = std::min(127., std::ceil(dimensions * std::log(16384. / (2. * pi)) /
                                               (2. * std::log(1000000.))));
    double maximum = 0, squared = 0, amplitude = 0, fp16_maximum = 0;
    auto actual = static_cast<float *>(output->data);
    for (int t = 0; t < positions; ++t) {
        for (int j = 0; j < dimensions / 2; ++j) {
            const double base = 1. / std::pow(1000000., j / 64.);
            const double mask = 1. - std::clamp((j - low) / (high - low), 0., 1.);
            const double frequency = base / 16. * (1. - mask) + base * mask;
            const double cosine = actual[t * dimensions + 2 * j];
            const double sine = actual[t * dimensions + 2 * j + 1];
            const double e0 = std::abs(cosine - std::cos(t * frequency));
            const double e1 = std::abs(sine - std::sin(t * frequency));
            // The compiled graph's external rotary inputs are FLOAT_16.
            const double half_cosine = ggml_fp16_to_fp32(ggml_fp32_to_fp16(float(std::cos(t * frequency))));
            const double half_sine = ggml_fp16_to_fp32(ggml_fp32_to_fp16(float(std::sin(t * frequency))));
            fp16_maximum = std::max({fp16_maximum, std::abs(cosine - half_cosine),
                                    std::abs(sine - half_sine)});
            maximum = std::max({maximum, e0, e1});
            squared += e0 * e0 + e1 * e1;
            amplitude = std::max(amplitude, std::abs(std::hypot(cosine, sine) - 1.));
        }
    }
    std::printf("{\"positions\":4096,\"rotary_pairs\":64,\"beta_low\":%.0f,"
                "\"beta_high\":%.0f,\"genie_attn_factor\":%.17g,\"max_abs\":%.12g,"
                "\"rms\":%.12g,\"max_amplitude_error\":%.12g,\"max_fp16_input_abs\":%.12g}\n",
                low, high, 1. / (1. + .1 * std::log(16.)), maximum,
                std::sqrt(squared / (dimensions * positions)), amplitude, fp16_maximum);
    ggml_free(context);
    // FP32 GGML's iterative frequencies differ slightly from Genie's doubles.
    return maximum < .001 && amplitude < 1.e-6 && fp16_maximum < .001 ? 0 : 1;
}
