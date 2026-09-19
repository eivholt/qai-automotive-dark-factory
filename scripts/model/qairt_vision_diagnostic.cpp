// Offline encoder parity diagnostic; never used by the live classifier.
#include <filesystem>
#include <fstream>
#include <iostream>
#include "qwen3_vl/qwen3_vl.h"
#include "vlm/vit_utils.h"
using namespace geniex;
namespace fs = std::filesystem;
void save(const fs::path& path, const std::vector<float>& values) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));
    if (!out) throw std::runtime_error("Cannot save diagnostic tensor");
}
int main(int argc, char** argv) {
    try {
        if (argc < 5) throw std::runtime_error("BUNDLE QAIRT_LIB OUTPUT_DIR IMAGE...");
        fs::path bundle=argv[1], libs=argv[2], output=argv[3];
        if (fs::exists(output)) throw std::runtime_error("Use a new output directory");
        auto meta = parseQAIRTMetadata(bundle);
        auto vp = *meta.vision_preprocessing;
        QnnRuntimeConfig runtime;
        runtime.backend_path = (libs / "libQnnHtp.so").string();
        runtime.system_lib_path = (libs / "libQnnSystem.so").string();
        runtime.extensions_path = (libs / "libQnnHtpNetRunExtensions.so").string();
        ModelConfig cfg;
        cfg.model_paths = {(bundle / "vision_encoder.bin").string()};
        cfg.htp_config_path = (bundle / "htp_backend_ext_config.json").string();
        qwen3_vl::Qwen3VLVisionEncoder encoder;
        encoder.setPreprocessing(vp);
        encoder.setHiddenSize(meta.hidden_size);
        if (!encoder.initialize(runtime, cfg)) throw std::runtime_error("Cannot load encoder");
        qwen2vl::Qwen2VLConfig pc;
        pc.fixed_height=vp.image_height; pc.fixed_width=vp.image_width;
        pc.patch_size=vp.patch_size; pc.temporal_patch_size=vp.temporal_patch_size;
        pc.merge_size=vp.spatial_merge_size; pc.image_mean=vp.normalize_mean; pc.image_std=vp.normalize_std;
        auto processor=qwen2vl::Qwen2VLProcessor::create((bundle / "tokenizer.json").string(), pc);
        std::vector<std::string> paths;
        std::string markers;
        for (int i=4;i<argc;++i) { paths.push_back(argv[i]); if(i>4) markers+='\n'; markers+="<__image__>"; }
        auto batch=processor->process(markers,paths);
        PixelData pd;
        pd.pixel_values.assign(batch.pixel_values.cbegin(),batch.pixel_values.cend());
        for(size_t i=0;i<batch.image_grid_thw.shape()[0];++i)
            pd.image_grid_thw.push_back({int32_t(batch.image_grid_thw(i,0)),int32_t(batch.image_grid_thw(i,1)),int32_t(batch.image_grid_thw(i,2))});
        auto features=encoder.encode(pd);
        fs::create_directories(output);
        save(output / "pixels.f32",pd.pixel_values);
        save(output / "features.f32",features);
        std::ifstream metadata_file(bundle / "metadata.json");
        qualla::json raw_metadata;
        metadata_file >> raw_metadata;
        const int rope_dim = raw_metadata["model_files"]["vision_encoder.bin"]["inputs"]["position_ids_cos"]["shape"].back();
        // Same runtime helper and theta used by Qwen3VLVisionEncoder::encode.
        auto rope = qwen_vit::computePatchRoPE(1, vp.image_height / vp.patch_size,
            vp.image_width / vp.patch_size, vp.spatial_merge_size, qwen_vit::makeInvFreq(rope_dim, 10000.0f));
        save(output / "rope_cos.f32", rope.first);
        save(output / "rope_sin.f32", rope.second);
        for(size_t i=0;i<encoder.deepstackEmbeds().size();++i)
            save(output / ("deepstack"+std::to_string(i)+".f32"),encoder.deepstackEmbeds()[i]);
        std::cout << "Saved encoder diagnostics: " << features.size() << " features\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
