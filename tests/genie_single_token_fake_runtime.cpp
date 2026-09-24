// Test-only Genie ABI shim. No model loading, QNN, accelerator, or real inference.
#include <Genie/GenieDialog.h>
#include <Genie/GenieLog.h>
#include <Genie/GenieSampler.h>
#include <Genie/GenieTokenizer.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

static GenieSampler_UserDataCallback_t sampler;
static const void* sampler_data;
static bool fresh = true;
static std::vector<uint32_t> evaluated;
extern "C" {
uint32_t Genie_getApiMajorVersion() { return 1; }
uint32_t Genie_getApiMinorVersion() {
    const char* version = std::getenv("DF_FAKE_GENIE_MINOR");
    return version ? uint32_t(std::strtoul(version, nullptr, 10)) : 20;
}
uint32_t Genie_getApiPatchVersion() { return 0; }
Genie_Status_t GenieSampler_registerUserDataCallback(const char*, GenieSampler_UserDataCallback_t f, const void* d) {
    sampler = f; sampler_data = d; return 0;
}
Genie_Status_t GenieLog_create(GenieLogConfig_Handle_t, GenieLog_Callback_t, GenieLog_Level_t, GenieLog_Handle_t* h) {
    *h = reinterpret_cast<GenieLog_Handle_t>(1); return 0;
}
Genie_Status_t GenieLog_free(GenieLog_Handle_t) { return 0; }
Genie_Status_t GenieDialogConfig_createFromJson(const char*, GenieDialogConfig_Handle_t* h) {
    *h = reinterpret_cast<GenieDialogConfig_Handle_t>(1); return 0;
}
Genie_Status_t GenieDialogConfig_bindLogger(GenieDialogConfig_Handle_t, GenieLog_Handle_t) { return 0; }
Genie_Status_t GenieDialogConfig_free(GenieDialogConfig_Handle_t) { return 0; }
Genie_Status_t GenieDialog_create(GenieDialogConfig_Handle_t, GenieDialog_Handle_t* h) {
    *h = reinterpret_cast<GenieDialog_Handle_t>(1); return 0;
}
Genie_Status_t GenieDialog_free(GenieDialog_Handle_t) { return 0; }
Genie_Status_t GenieDialog_setMaxNumTokens(GenieDialog_Handle_t, uint32_t n) { return n == 1 ? 0 : -2; }
Genie_Status_t GenieDialog_getTokenizer(GenieDialog_Handle_t, GenieTokenizer_Handle_t* h) {
    *h = reinterpret_cast<GenieTokenizer_Handle_t>(1); return 0;
}
Genie_Status_t GenieDialog_reset(GenieDialog_Handle_t) { fresh = true; evaluated.clear(); return 0; }
Genie_Status_t GenieDialog_getValue(GenieDialog_Handle_t, GenieDialog_Param_t key, Genie_AllocCallback_t,
                                  Genie_DataType_t* type, Genie_Value_t* value) {
    if (key != GENIE_DIALOG_PARAM_CONTEXT_OCCUPANCY) return -2;
    *type = GENIE_DATATYPE_UINT_32; value->uint32Value = uint32_t(evaluated.size()); return 0;
}
Genie_Status_t GenieTokenizer_encode(GenieTokenizer_Handle_t, const char* s, Genie_AllocCallback_t allocate,
                                    const int32_t** tokens, uint32_t* count) {
    std::vector<int32_t> out;
    for (const unsigned char c : std::string(s)) out.push_back(c >= 'A' && c <= 'M' ? 1065 + c - 'A' : c);
    const char* raw; allocate(out.size() * sizeof(int32_t), &raw);
    std::memcpy(const_cast<char*>(raw), out.data(), out.size() * sizeof(int32_t));
    *tokens = reinterpret_cast<const int32_t*>(raw); *count = uint32_t(out.size()); return 0;
}
Genie_Status_t GenieTokenizer_decode(GenieTokenizer_Handle_t, const int32_t* tokens, uint32_t count,
                                    Genie_AllocCallback_t allocate, const char** text) {
    std::string out;
    for (uint32_t i = 0; i < count; ++i)
        out += tokens[i] >= 1065 && tokens[i] <= 1077 ? std::string(1, char('A' + tokens[i] - 1065)) : "?";
    allocate(out.size() + 1, text); std::memcpy(const_cast<char*>(*text), out.c_str(), out.size() + 1); return 0;
}
Genie_Status_t GenieDialog_tokenQuery(GenieDialog_Handle_t, const uint32_t* input, uint32_t count,
                                    GenieDialog_SentenceCode_t code, GenieDialog_TokenQueryCallback_t callback,
                                    const void* user) {
    if (!count) return -2;
    if (code == GENIE_DIALOG_SENTENCE_REWIND) {
        if (fresh || Genie_getApiMinorVersion() != 20) return -2;
        size_t common = 0;
        while (common < evaluated.size() && common < count && evaluated[common] == input[common]) ++common;
        if (!common || common == evaluated.size() || common == count) return -2;
        evaluated.assign(input, input + count);
    } else {
        if (code != GENIE_DIALOG_SENTENCE_END || (!fresh && input[0] != 1065)) return -2;
        evaluated.insert(evaluated.end(), input, input + count);
    }
    fresh = false;
    auto has = [&](uint32_t id) { return std::find(input, input + count, id) != input + count; };
    if (has(676)) { // Simulate the unsafe native boundary path: saved token, no fresh sampler.
        const uint32_t saved = 1065;
        callback(&saved, 1, GENIE_DIALOG_SENTENCE_END, user);
        return 0;
    }
    std::vector<float> logits(2048, -10.0f);
    // Full evaluated sequence affects scores, making wrong suffix/repeated BOS
    // observable in a reset-versus-append protocol comparison.
    float offset = 0; for (uint32_t token : evaluated) offset += float(token % 11) * .001f;
    const uint32_t winner = has(670) || has(672) || has(673) ? 2 : has(674) ? 3 :
        has(675) ? 4 : has(666) ? 230 : 1065;
    logits[winner] = 5 + offset;
    if (has(668)) logits[5] = std::numeric_limits<float>::quiet_NaN();
    int32_t chosen = 0;
    sampler(uint32_t(logits.size() * (has(669) ? 1 : sizeof(float))), logits.data(), 1, &chosen, sampler_data);
    if (has(672)) return 0; // Missing callback must not be mistaken for EOS suppression.
    if (has(670) || has(671) || has(673) || has(674) || has(675)) {
        callback(nullptr, 0, has(673) ? GENIE_DIALOG_SENTENCE_ABORT : GENIE_DIALOG_SENTENCE_END, user);
        return 0;
    }
    uint32_t token = uint32_t(chosen + (has(667) ? 1 : 0));
    if (has(677)) { // Matching fresh token is still incomplete without a native END.
        callback(&token, 1, GENIE_DIALOG_SENTENCE_BEGIN, user);
        return 0;
    }
    callback(&token, 1, GENIE_DIALOG_SENTENCE_END, user); return 0;
}
}
