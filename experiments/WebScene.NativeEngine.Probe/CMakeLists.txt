cmake_minimum_required(VERSION 3.20)
project(WebSceneNativeEngineProbe LANGUAGES C CXX)

include(CTest)
include(FetchContent)

if(MSVC)
    # V8's Windows monolith uses the static CRT. Set the directory default
    # before FetchContent creates IXWebSocket/Mbed TLS targets so the complete
    # native dependency graph uses the same runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(webscene_native_engine SHARED
    native/webscene_native_engine.cpp
    native/webscene_native_dom.cpp)

target_compile_features(webscene_native_engine PRIVATE cxx_std_20)
target_compile_definitions(webscene_native_engine PRIVATE WEBSCENE_NATIVE_ENGINE_BUILD)
target_include_directories(webscene_native_engine PUBLIC native)

option(WEBSCENE_NATIVE_ENGINE_ENABLE_V8 "Link the engine directly to the pinned V8 monolith" OFF)
option(WEBSCENE_V8_POINTER_COMPRESSION
    "Compile the embedder ABI for a pointer-compressed V8 monolith" OFF)
option(WEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE
    "Compile the embedder ABI for V8's process-wide shared compression cage" OFF)
option(WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT
    "Enable V8's optimize-for-size process policy unless the host overrides it" OFF)
option(WEBSCENE_NATIVE_ENGINE_DENSE_LINK
    "Dead-strip unused native code and expose only the WebScene C ABI" OFF)
option(WEBSCENE_NATIVE_ENGINE_CERTIFICATION
    "Include certification telemetry, diagnostic snapshots, and native profiling hooks" OFF)

if(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    target_compile_definitions(webscene_native_engine PUBLIC
        WEBSCENE_NATIVE_ENGINE_CERTIFICATION=1)
    message(STATUS "WebScene native engine: certification telemetry enabled")
else()
    message(STATUS "WebScene native engine: certification telemetry excluded")
endif()
set(WEBSCENE_V8_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/clearscript/V8/build/v8"
    CACHE PATH
    "Path to the pinned V8 source/build root")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" WEBSCENE_SYSTEM_PROCESSOR_LOWER)
string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" WEBSCENE_GENERATOR_PLATFORM_LOWER)
if(WEBSCENE_GENERATOR_PLATFORM_LOWER STREQUAL "arm64")
    set(WEBSCENE_V8_CPU arm64)
elseif(WEBSCENE_GENERATOR_PLATFORM_LOWER STREQUAL "x64")
    set(WEBSCENE_V8_CPU x64)
elseif(WEBSCENE_SYSTEM_PROCESSOR_LOWER MATCHES "^(arm64|aarch64)$")
    set(WEBSCENE_V8_CPU arm64)
elseif(WEBSCENE_SYSTEM_PROCESSOR_LOWER MATCHES "^(x86_64|amd64)$")
    set(WEBSCENE_V8_CPU x64)
else()
    set(WEBSCENE_V8_CPU "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(MSVC)
    set(WEBSCENE_V8_MONOLITH_FILENAME "v8_monolith.lib")
else()
    set(WEBSCENE_V8_MONOLITH_FILENAME "libv8_monolith.a")
endif()
set(WEBSCENE_V8_OUTPUT_ROOT
    "${WEBSCENE_V8_ROOT}/out/${WEBSCENE_V8_CPU}/Release"
    CACHE PATH
    "Path to the pinned V8 GN output directory")
set(WEBSCENE_V8_MONOLITH
    "${WEBSCENE_V8_OUTPUT_ROOT}/obj/${WEBSCENE_V8_MONOLITH_FILENAME}")
set(WEBSCENE_V8_ICU_DATA
    "${WEBSCENE_V8_OUTPUT_ROOT}/icudtl.dat")

if(WEBSCENE_NATIVE_ENGINE_ENABLE_V8)
    if(NOT EXISTS "${WEBSCENE_V8_ROOT}/include/v8.h")
        message(FATAL_ERROR "WebScene native engine: V8 headers not found below ${WEBSCENE_V8_ROOT}")
    endif()
    if(NOT EXISTS "${WEBSCENE_V8_MONOLITH}")
        message(FATAL_ERROR "WebScene native engine: V8 monolith not found at ${WEBSCENE_V8_MONOLITH}")
    endif()
    if(NOT EXISTS "${WEBSCENE_V8_ICU_DATA}")
        message(FATAL_ERROR "WebScene native engine: ICU data not found at ${WEBSCENE_V8_ICU_DATA}")
    endif()

    # WebSocket I/O is a native implementation detail. IXWebSocket supplies a
    # portable RFC 6455 client while the WebScene adapter below owns all V8
    # lifetime/threading semantics. Pin both source and digest so release
    # builds do not float with upstream.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(USE_TLS ON CACHE BOOL "" FORCE)
    if(WIN32)
        # Windows release runners do not carry a stable OpenSSL SDK. Build the
        # pinned Mbed TLS release and give IXWebSocket its normal CMake targets.
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            webscene_mbedtls
            URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.4/mbedtls-3.6.4.tar.bz2
            URL_HASH SHA256=ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(webscene_mbedtls)
        set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
        set(USE_ZLIB OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FOUND TRUE)
        set(MBEDTLS_VERSION_GREATER_THAN_3 TRUE)
        set(MBEDTLS_INCLUDE_DIRS "${webscene_mbedtls_SOURCE_DIR}/include")
        set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
    else()
        set(USE_ZLIB ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        webscene_ixwebsocket
        URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v12.0.1.tar.gz
        URL_HASH SHA256=d23bdc91dbfe2b9ae13c322d539392d7a6b8b506560f41c90e227fa0f86a2405
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(webscene_ixwebsocket)

    target_sources(webscene_native_engine PRIVATE
        native/webscene_v8_runtime.cpp
        native/webscene_native_websocket.cpp)
    target_include_directories(webscene_native_engine SYSTEM PRIVATE "${WEBSCENE_V8_ROOT}/include")
    target_compile_definitions(webscene_native_engine PRIVATE
        WEBSCENE_NATIVE_ENGINE_WITH_V8
        WEBSCENE_V8_ICU_DATA_FILENAME="icudtl.dat")
    if(WEBSCENE_V8_POINTER_COMPRESSION)
        target_compile_definitions(webscene_native_engine PRIVATE
            V8_COMPRESS_POINTERS
            V8_31BIT_SMIS_ON_64BIT_ARCH)
    endif()
    if(WEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE)
        if(NOT WEBSCENE_V8_POINTER_COMPRESSION)
            message(FATAL_ERROR
                "WEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE requires "
                "WEBSCENE_V8_POINTER_COMPRESSION")
        endif()
        target_compile_definitions(webscene_native_engine PRIVATE
            V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
    endif()
    if(WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT)
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT=1)
    endif()
    target_link_libraries(webscene_native_engine PRIVATE
        "${WEBSCENE_V8_MONOLITH}"
        ixwebsocket)
    if(APPLE)
        target_link_libraries(webscene_native_engine PRIVATE "-framework CoreFoundation")
    elseif(WIN32)
        target_link_libraries(webscene_native_engine PRIVATE dbghelp winmm advapi32)
    elseif(UNIX)
        target_link_libraries(webscene_native_engine PRIVATE dl rt)
    endif()
    message(STATUS "WebScene native engine: V8 enabled (${WEBSCENE_V8_MONOLITH})")

    if(BUILD_TESTING)
        # Direct native_document unit fixtures remain local to the test
        # executable instead of widening the production C ABI export surface.
        add_executable(webscene_native_engine_tests
            tests/native_v8_runtime_tests.cpp
            native/webscene_native_dom.cpp)
        target_compile_features(webscene_native_engine_tests PRIVATE cxx_std_20)
        target_link_libraries(webscene_native_engine_tests PRIVATE
            webscene_native_engine
            ixwebsocket)
        add_test(NAME webscene_native_engine_tests COMMAND webscene_native_engine_tests)
    endif()
else()
    message(STATUS "WebScene native engine: V8 intentionally disabled")
endif()

if(MSVC)
    # The pinned V8 Windows monolith is built with the static MSVC runtime
    # (/MT). Match it here so the DLL does not mix CRT/STL implementations
    # and fail with LNK2038/LNK2005 at link time.
    set_property(TARGET webscene_native_engine PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # MSVC reports the historical C++98 value through __cplusplus unless this
    # conformance switch is enabled. V8 checks __cplusplus directly when
    # enforcing its C++20 embedder requirement.
    target_compile_options(webscene_native_engine PRIVATE
        /W4 /permissive- /GR- /wd4996 /std:c++20 /Zc:__cplusplus)
    if(TARGET webscene_native_engine_tests)
        set_property(TARGET webscene_native_engine_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(webscene_native_engine_tests PRIVATE
            /std:c++20 /Zc:__cplusplus)
    endif()
else()
    target_compile_options(webscene_native_engine PRIVATE
        -Wall -Wextra -Wpedantic -fno-rtti -Wno-deprecated-declarations)
endif()

if(WEBSCENE_NATIVE_ENGINE_DENSE_LINK)
    if(MSVC)
        target_link_options(webscene_native_engine PRIVATE
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>)
    else()
        target_compile_options(webscene_native_engine PRIVATE
            -ffunction-sections
            -fdata-sections
            -fvisibility=hidden
            -fvisibility-inlines-hidden)
        if(APPLE)
            target_link_options(webscene_native_engine PRIVATE
                "LINKER:-dead_strip"
                "LINKER:-x"
                "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/native/webscene_native_engine.exports")
        elseif(UNIX)
            target_link_options(webscene_native_engine PRIVATE
                "LINKER:--gc-sections"
                "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/native/webscene_native_engine.map"
                "LINKER:-x")
        endif()
    endif()
endif()
