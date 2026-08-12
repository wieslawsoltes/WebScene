#include "webscene_gpu_provider.h"
#include "webscene_native_engine.h"

#include <cassert>
#include <cstring>

int main(int argc, char** argv)
{
    assert(argc == 2);
    assert((webscene_engine_get_build_features()
        & WEBSCENE_ENGINE_BUILD_FEATURE_GPU_PROVIDER_ABI) != 0U);
    const auto* provider_path = argv[1];

    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.gpu_provider_path = provider_path;
    options.gpu_provider_path_length = std::strlen(provider_path);
    options.required_capabilities = 0U;
    auto* engine = webscene_engine_create_with_options(&options);
    assert(engine != nullptr);
    assert(webscene_engine_get_capabilities(engine) == 0U);
    webscene_engine_destroy(engine);

    options.required_capabilities = WEBSCENE_GPU_CAPABILITY_WEBGPU;
    assert(webscene_engine_create_with_options(&options) == nullptr);

    options.gpu_provider_path = nullptr;
    options.gpu_provider_path_length = 0U;
    options.required_capabilities = WEBSCENE_GPU_CAPABILITY_WEBGPU;
    assert(webscene_engine_create_with_options(&options) == nullptr);
    return 0;
}
