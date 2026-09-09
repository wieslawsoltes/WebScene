# A small, pinned audio dependency, compiled into WebScene rather than shipped
# as another third-party dylib. FetchContent caches the verified source archive.
FetchContent_Declare(webscene_miniaudio
    URL https://codeload.github.com/mackron/miniaudio/tar.gz/f40cf03f80cdb7e741d43e53b7e706e8c1394bcf
    URL_HASH SHA256=412326cf55133404cbfb81ec8974b10149dc68732ce5bdeee8ba9cfc2695d646)
FetchContent_GetProperties(webscene_miniaudio)
if(NOT webscene_miniaudio_POPULATED)
    FetchContent_Populate(webscene_miniaudio)
endif()
add_library(webscene_media STATIC audio_decode.cpp miniaudio_impl.c)
target_compile_features(webscene_media PUBLIC cxx_std_20)
target_include_directories(webscene_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(webscene_media SYSTEM PRIVATE "${webscene_miniaudio_SOURCE_DIR}")
set_target_properties(webscene_media PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
# Only decoding is enabled in this first foundation slice. Audio devices/graph
# will be enabled with the Web Audio realtime service, not by opening a device
# implicitly during a file decode.
target_compile_definitions(webscene_media PRIVATE MA_NO_DEVICE_IO MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_GENERATION)
find_package(Threads REQUIRED)
target_link_libraries(webscene_media PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(APPLE)
    enable_language(OBJCXX)
    target_sources(webscene_media PRIVATE video_decode_macos.mm)
    set_source_files_properties(video_decode_macos.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(webscene_media PUBLIC "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation")
else()
    target_sources(webscene_media PRIVATE video_decode_unsupported.cpp)
endif()
configure_file("${webscene_miniaudio_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/webscene-miniaudio-LICENSE" COPYONLY)
if(BUILD_TESTING)
    add_executable(webscene_media_decode_tests ../../tests/media_decode_tests.cpp)
    target_link_libraries(webscene_media_decode_tests PRIVATE webscene_media)
    if(APPLE)
        add_test(NAME webscene_media_decode_tests COMMAND webscene_media_decode_tests "-"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/media/numbered-motion.mp4")
    else()
        add_test(NAME webscene_media_decode_tests COMMAND webscene_media_decode_tests)
    endif()
endif()
