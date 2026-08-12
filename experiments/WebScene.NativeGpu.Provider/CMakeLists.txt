cmake_minimum_required(VERSION 3.22)
project(WebSceneNativeGpuProvider LANGUAGES C CXX OBJCXX)

include(CTest)
find_package(Threads REQUIRED)
find_package(Dawn CONFIG REQUIRED)

add_library(webscene_native_gpu SHARED
    native/webscene_gpu_provider_dawn.mm)
target_compile_features(webscene_native_gpu PRIVATE cxx_std_20)
target_compile_definitions(webscene_native_gpu PRIVATE
    WEBSCENE_GPU_PROVIDER_BUILD=1)
target_compile_options(webscene_native_gpu PRIVATE -fno-objc-arc)
target_include_directories(webscene_native_gpu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../WebScene.NativeEngine.Probe/native")
target_link_libraries(webscene_native_gpu PRIVATE
    dawn::webgpu_dawn
    "-framework CoreFoundation"
    "-framework CoreVideo"
    "-framework Foundation"
    "-framework IOSurface"
    "-framework Metal")
set_target_properties(webscene_native_gpu PROPERTIES
    OUTPUT_NAME webscene_native_gpu
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(BUILD_TESTING)
    add_executable(webscene_native_gpu_smoke_tests
        tests/provider_smoke_tests.cpp)
    target_compile_features(webscene_native_gpu_smoke_tests PRIVATE cxx_std_20)
    target_include_directories(webscene_native_gpu_smoke_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../WebScene.NativeEngine.Probe/native")
    target_link_libraries(webscene_native_gpu_smoke_tests PRIVATE
        webscene_native_gpu
        dawn::webgpu_dawn)
    add_test(NAME webscene_native_gpu_smoke_tests
        COMMAND webscene_native_gpu_smoke_tests)
endif()
