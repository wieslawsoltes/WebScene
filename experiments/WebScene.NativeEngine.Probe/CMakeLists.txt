cmake_minimum_required(VERSION 3.20)
project(WebSceneNativeEngineProbe LANGUAGES C CXX)

include(CTest)
include(FetchContent)

# CMake 3.24 made extracted archive timestamps deterministic through CMP0135.
# Use the policy when available while remaining compatible with the Ubuntu
# 22.04 runner's CMake 3.22, which does not recognize
# DOWNLOAD_EXTRACT_TIMESTAMP as a FetchContent argument.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
# CMake 3.24's CMP0138 makes CheckIPOSupported validate with the caller's
# compiler and linker flags. ThinLTO needs this so the check uses Chromium LLD
# with the same versioned bitcode toolchain as V8.
if(POLICY CMP0138)
    cmake_policy(SET CMP0138 NEW)
endif()

if(MSVC)
    # V8's Windows monolith uses the static CRT. Set the directory default
    # before FetchContent creates IXWebSocket/Mbed TLS targets so the complete
    # native dependency graph uses the same runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(webscene_native_engine SHARED
    native/webscene_gpu_provider_loader.cpp
    native/webscene_native_engine.cpp
    native/webscene_native_dom.cpp)

target_compile_features(webscene_native_engine PRIVATE cxx_std_20)
target_compile_definitions(webscene_native_engine PRIVATE WEBSCENE_NATIVE_ENGINE_BUILD)
target_include_directories(webscene_native_engine PUBLIC native)
if(CMAKE_DL_LIBS)
    target_link_libraries(webscene_native_engine PRIVATE ${CMAKE_DL_LIBS})
endif()

option(WEBSCENE_NATIVE_ENGINE_ENABLE_V8 "Link the engine directly to the pinned V8 monolith" OFF)
option(WEBSCENE_NATIVE_ENGINE_ENABLE_WEBGPU
    "Compile the Dawn WebGPU V8 bindings and zero-copy canvas bridge" OFF)
set(WEBSCENE_DAWN_INCLUDE_DIR "" CACHE PATH
    "Directory containing the pinned Dawn webgpu/webgpu.h header")
option(WEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR
    "Compile V8 Inspector state, hooks, queues, and worker dispatch into the runtime"
    "${WEBSCENE_NATIVE_ENGINE_ENABLE_V8}")
option(WEBSCENE_V8_POINTER_COMPRESSION
    "Compile the embedder ABI for a pointer-compressed V8 monolith" OFF)
option(WEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE
    "Compile the embedder ABI for V8's process-wide shared compression cage" OFF)
option(WEBSCENE_V8_DIRECT_HANDLE
    "Compile the embedder ABI for a V8 monolith built with direct handles" OFF)
option(WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT
    "Enable V8's optimize-for-size process policy unless the host overrides it" OFF)
option(WEBSCENE_V8_PARTITION_ALLOC
    "Configure a V8 monolith built with PartitionAlloc and its malloc shim" OFF)
option(WEBSCENE_NATIVE_ENGINE_DENSE_LINK
    "Dead-strip unused native code and expose only the WebScene C ABI" OFF)
option(WEBSCENE_NATIVE_ENGINE_THIN_LTO
    "Enable ThinLTO for the WebScene native engine and its V8 monolith link" OFF)
option(WEBSCENE_NATIVE_ENGINE_CERTIFICATION
    "Include certification telemetry, diagnostic snapshots, and native profiling hooks" OFF)
option(WEBSCENE_NATIVE_ENGINE_BUILD_HTML_PARSER_BENCHMARK
    "Build the V8-free html5ever parser microbenchmark" OFF)
option(WEBSCENE_NATIVE_ENGINE_BUILD_CSS_PARSER_BENCHMARK
    "Build the V8-free selected CSS syntax parser microbenchmark" OFF)
option(WEBSCENE_NATIVE_ENGINE_BUILD_DOM_BINDING_BENCHMARK
    "Build the native V8 DOM binding lifecycle benchmark" OFF)
set(WEBSCENE_GPU_PROVIDER_TEST_LIBRARY "" CACHE FILEPATH
    "Optional ABI-compatible GPU provider used by the native WebGPU triangle test")
set(WEBSCENE_THREE_WEBGPU_TEST_SCRIPT "" CACHE FILEPATH
    "Optional pinned Three.js IIFE bundle appended to the native WebGPU smoke test")
set(WEBSCENE_WEBGPU_CTS_TEST_SCRIPT "" CACHE FILEPATH
    "Optional curated WebGPU CTS adaptation appended to the native WebGPU smoke test")
set(WEBSCENE_NATIVE_ENGINE_HTML_PARSER "html5ever" CACHE STRING
    "Native HTML parser implementation: legacy or html5ever")
set_property(CACHE WEBSCENE_NATIVE_ENGINE_HTML_PARSER PROPERTY STRINGS legacy html5ever)
set(WEBSCENE_NATIVE_ENGINE_CSS_PARSER "cssparser" CACHE STRING
    "Native CSS syntax parser implementation: legacy or cssparser")
set_property(CACHE WEBSCENE_NATIVE_ENGINE_CSS_PARSER PROPERTY STRINGS legacy cssparser)
set(WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER "servo" CACHE STRING
    "Native CSS selector parser implementation: legacy or servo")
set_property(CACHE WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER PROPERTY STRINGS legacy servo)
set(WEBSCENE_NATIVE_ENGINE_DOM_BINDINGS "generated" CACHE STRING
    "Native DOM binding catalog: legacy or generated")
set_property(CACHE WEBSCENE_NATIVE_ENGINE_DOM_BINDINGS PROPERTY STRINGS legacy generated)
set(WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT "bootstrap" CACHE STRING
    "V8 embedder startup snapshot: none or bootstrap")
set_property(CACHE WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT PROPERTY STRINGS none bootstrap)

if(WEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR
    AND NOT WEBSCENE_NATIVE_ENGINE_ENABLE_V8)
    message(FATAL_ERROR
        "WEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR requires "
        "WEBSCENE_NATIVE_ENGINE_ENABLE_V8")
endif()

if(WEBSCENE_NATIVE_ENGINE_ENABLE_WEBGPU)
    if(NOT WEBSCENE_NATIVE_ENGINE_ENABLE_V8)
        message(FATAL_ERROR
            "WEBSCENE_NATIVE_ENGINE_ENABLE_WEBGPU requires V8")
    endif()
    if(NOT EXISTS "${WEBSCENE_DAWN_INCLUDE_DIR}/webgpu/webgpu.h")
        message(FATAL_ERROR
            "WEBSCENE_NATIVE_ENGINE_ENABLE_WEBGPU requires the pinned Dawn include directory")
    endif()
    target_include_directories(webscene_native_engine SYSTEM PRIVATE
        "${WEBSCENE_DAWN_INCLUDE_DIR}")
    target_compile_definitions(webscene_native_engine PRIVATE
        WEBSCENE_NATIVE_ENGINE_WEBGPU=1)
    message(STATUS "WebScene native engine: WebGPU bindings enabled")
endif()

if(WEBSCENE_NATIVE_ENGINE_DOM_BINDINGS STREQUAL "generated")
    target_compile_definitions(webscene_native_engine PRIVATE
        WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS=1)
    message(STATUS "WebScene native engine: generated WebIDL DOM bindings selected")
elseif(NOT WEBSCENE_NATIVE_ENGINE_DOM_BINDINGS STREQUAL "legacy")
    message(FATAL_ERROR
        "WEBSCENE_NATIVE_ENGINE_DOM_BINDINGS must be legacy or generated")
else()
    message(STATUS "WebScene native engine: legacy DOM bindings selected")
endif()

if(WEBSCENE_NATIVE_ENGINE_CSS_PARSER STREQUAL "cssparser"
    AND NOT WEBSCENE_NATIVE_ENGINE_HTML_PARSER STREQUAL "html5ever")
    message(FATAL_ERROR
        "The cssparser implementation requires the shared html5ever Rust library")
elseif(NOT WEBSCENE_NATIVE_ENGINE_CSS_PARSER STREQUAL "legacy"
    AND NOT WEBSCENE_NATIVE_ENGINE_CSS_PARSER STREQUAL "cssparser")
    message(FATAL_ERROR
        "WEBSCENE_NATIVE_ENGINE_CSS_PARSER must be legacy or cssparser")
endif()

if(WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER STREQUAL "servo"
    AND NOT WEBSCENE_NATIVE_ENGINE_HTML_PARSER STREQUAL "html5ever")
    message(FATAL_ERROR
        "The Servo selector implementation requires the shared html5ever Rust library")
elseif(NOT WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER STREQUAL "legacy"
    AND NOT WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER STREQUAL "servo")
    message(FATAL_ERROR
        "WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER must be legacy or servo")
endif()

if(WEBSCENE_NATIVE_ENGINE_HTML_PARSER STREQUAL "html5ever")
    find_program(WEBSCENE_CARGO_EXECUTABLE cargo REQUIRED)
    set(WEBSCENE_ENABLE_CSSPARSER_RUST OFF)
    set(WEBSCENE_HTML_PARSER_CARGO_FEATURES)
    if(WEBSCENE_NATIVE_ENGINE_CSS_PARSER STREQUAL "cssparser")
        set(WEBSCENE_ENABLE_CSSPARSER_RUST ON)
        list(APPEND WEBSCENE_HTML_PARSER_CARGO_FEATURES
            --features cssparser)
    endif()
    if(WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER STREQUAL "servo")
        list(APPEND WEBSCENE_HTML_PARSER_CARGO_FEATURES
            --features selectors)
    endif()
    set(WEBSCENE_HTML_PARSER_MANIFEST
        "${CMAKE_CURRENT_SOURCE_DIR}/native/html_parser/Cargo.toml")
    set(WEBSCENE_HTML_PARSER_TARGET_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/html-parser-target")
    if(MSVC)
        set(WEBSCENE_HTML_PARSER_LIBRARY
            "${WEBSCENE_HTML_PARSER_TARGET_DIR}/release/webscene_html_parser.lib")
    else()
        set(WEBSCENE_HTML_PARSER_LIBRARY
            "${WEBSCENE_HTML_PARSER_TARGET_DIR}/release/libwebscene_html_parser.a")
    endif()
    add_custom_command(
        OUTPUT "${WEBSCENE_HTML_PARSER_LIBRARY}"
        COMMAND ${CMAKE_COMMAND} -E env
            "CARGO_TARGET_DIR=${WEBSCENE_HTML_PARSER_TARGET_DIR}"
            "${WEBSCENE_CARGO_EXECUTABLE}" build
            --manifest-path "${WEBSCENE_HTML_PARSER_MANIFEST}"
            --release --locked ${WEBSCENE_HTML_PARSER_CARGO_FEATURES}
        DEPENDS
            "${WEBSCENE_HTML_PARSER_MANIFEST}"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/html_parser/Cargo.lock"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/html_parser/src/lib.rs"
        COMMENT "Building the pinned html5ever WebScene parser"
        VERBATIM)
    add_custom_target(webscene_html_parser_build
        DEPENDS "${WEBSCENE_HTML_PARSER_LIBRARY}")
    add_library(webscene_html_parser STATIC IMPORTED GLOBAL)
    set_target_properties(webscene_html_parser PROPERTIES
        IMPORTED_LOCATION "${WEBSCENE_HTML_PARSER_LIBRARY}")
    add_dependencies(webscene_html_parser webscene_html_parser_build)
    if(WIN32)
        target_link_libraries(webscene_html_parser INTERFACE
            advapi32 bcrypt ntdll userenv ws2_32)
    elseif(APPLE)
        target_link_libraries(webscene_html_parser INTERFACE iconv)
    elseif(UNIX)
        target_link_libraries(webscene_html_parser INTERFACE
            dl m pthread rt util)
    endif()
    target_sources(webscene_native_engine PRIVATE
        native/webscene_html_parser.cpp)
    target_link_libraries(webscene_native_engine PRIVATE webscene_html_parser)
    target_compile_definitions(webscene_native_engine PRIVATE
        WEBSCENE_NATIVE_ENGINE_HTML5EVER=1)
    if(WEBSCENE_NATIVE_ENGINE_CSS_PARSER STREQUAL "cssparser")
        target_sources(webscene_native_engine PRIVATE
            native/webscene_css_parser.cpp)
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_NATIVE_ENGINE_CSSPARSER=1)
        message(STATUS "WebScene native engine: cssparser syntax parser selected")
    else()
        message(STATUS "WebScene native engine: legacy CSS syntax parser selected")
    endif()
    if(WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER STREQUAL "servo")
        target_sources(webscene_native_engine PRIVATE
            native/webscene_selector_parser.cpp)
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS=1)
        message(STATUS "WebScene native engine: Servo selector parser selected")
    else()
        message(STATUS "WebScene native engine: legacy selector parser selected")
    endif()
    if(BUILD_TESTING)
        add_executable(webscene_html_parser_tests
            tests/html_parser_tests.cpp
            native/webscene_html_parser.cpp
            native/webscene_native_dom.cpp)
        target_compile_features(webscene_html_parser_tests PRIVATE cxx_std_20)
        target_include_directories(webscene_html_parser_tests PRIVATE native)
        target_link_libraries(webscene_html_parser_tests PRIVATE webscene_html_parser)
        add_test(NAME webscene_html_parser_tests COMMAND webscene_html_parser_tests)
        if(WEBSCENE_ENABLE_CSSPARSER_RUST)
            add_executable(webscene_css_parser_tests
                tests/css_parser_tests.cpp
                native/webscene_css_parser.cpp)
            target_compile_features(webscene_css_parser_tests PRIVATE cxx_std_20)
            target_include_directories(webscene_css_parser_tests PRIVATE native)
            target_link_libraries(webscene_css_parser_tests PRIVATE webscene_html_parser)
            add_test(NAME webscene_css_parser_tests COMMAND webscene_css_parser_tests)
        endif()
        if(WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER STREQUAL "servo")
            add_executable(webscene_selector_parser_tests
                tests/selector_parser_tests.cpp
                native/webscene_selector_parser.cpp)
            target_compile_features(webscene_selector_parser_tests PRIVATE cxx_std_20)
            target_include_directories(webscene_selector_parser_tests PRIVATE native)
            target_link_libraries(webscene_selector_parser_tests PRIVATE webscene_html_parser)
            add_test(NAME webscene_selector_parser_tests COMMAND webscene_selector_parser_tests)
        endif()
    endif()
    if(WEBSCENE_NATIVE_ENGINE_BUILD_HTML_PARSER_BENCHMARK)
        add_executable(webscene_html_parser_benchmark
            benchmarks/html_parser_benchmark.cpp
            native/webscene_html_parser.cpp
            native/webscene_native_dom.cpp)
        target_compile_features(webscene_html_parser_benchmark PRIVATE cxx_std_20)
        target_include_directories(webscene_html_parser_benchmark PRIVATE native)
        target_link_libraries(webscene_html_parser_benchmark PRIVATE webscene_html_parser)
    endif()
    if(WEBSCENE_NATIVE_ENGINE_BUILD_CSS_PARSER_BENCHMARK)
        if(WEBSCENE_NATIVE_ENGINE_CSS_PARSER STREQUAL "legacy")
            message(FATAL_ERROR
                "The CSS parser benchmark requires cssparser")
        endif()
        add_executable(webscene_css_parser_benchmark
            benchmarks/css_parser_benchmark.cpp
            native/webscene_css_parser.cpp)
        target_compile_features(webscene_css_parser_benchmark PRIVATE cxx_std_20)
        target_include_directories(webscene_css_parser_benchmark PRIVATE native)
        target_link_libraries(webscene_css_parser_benchmark PRIVATE webscene_html_parser)
    endif()
    message(STATUS "WebScene native engine: html5ever parser selected")
elseif(NOT WEBSCENE_NATIVE_ENGINE_HTML_PARSER STREQUAL "legacy")
    message(FATAL_ERROR
        "WEBSCENE_NATIVE_ENGINE_HTML_PARSER must be legacy or html5ever")
else()
    message(STATUS "WebScene native engine: legacy HTML parser selected")
endif()

if(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    target_compile_definitions(webscene_native_engine PUBLIC
        WEBSCENE_NATIVE_ENGINE_CERTIFICATION=1)
    message(STATUS "WebScene native engine: certification telemetry enabled")
else()
    message(STATUS "WebScene native engine: certification telemetry excluded")
endif()
set(WEBSCENE_V8_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/v8"
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
    if(WEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR)
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR=1)
        message(STATUS "WebScene native engine: V8 Inspector support enabled")
    else()
        message(STATUS "WebScene native engine: V8 Inspector compiled out")
    endif()
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
            URL_HASH SHA256=ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110)
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
    # WebScene embeds IXWebSocket and ships only its own runtime library.
    # Disable IXWebSocket's standalone install export, which otherwise tries
    # to export private Mbed TLS targets on Windows.
    set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        webscene_ixwebsocket
        URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v12.0.1.tar.gz
        URL_HASH SHA256=d23bdc91dbfe2b9ae13c322d539392d7a6b8b506560f41c90e227fa0f86a2405)
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
    if(WEBSCENE_V8_DIRECT_HANDLE)
        target_compile_definitions(webscene_native_engine PRIVATE
            V8_ENABLE_DIRECT_HANDLE)
    endif()
    if(WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT)
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT=1)
    endif()
    if(WEBSCENE_V8_PARTITION_ALLOC)
        if(NOT EXISTS
            "${WEBSCENE_V8_OUTPUT_ROOT}/gen/third_party/partition_alloc/src/partition_alloc/buildflags.h")
            message(FATAL_ERROR
                "WEBSCENE_V8_PARTITION_ALLOC requires a V8 output built with "
                "PartitionAlloc")
        endif()
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_V8_PARTITION_ALLOC=1)
        target_include_directories(webscene_native_engine SYSTEM PRIVATE
            "${WEBSCENE_V8_ROOT}/third_party/partition_alloc/src"
            "${WEBSCENE_V8_OUTPUT_ROOT}/gen/third_party/partition_alloc/src")
    endif()

    if(WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT STREQUAL "bootstrap")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(WEBSCENE_V8_BOOTSTRAP_SOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/webscene_v8_bootstrap.js")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/native/webscene_v8_runtime.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/webscene_v8_runtime_webgpu.inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/v8-snapshot/extract_bootstraps.py")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/v8-snapshot/extract_bootstraps.py"
                --input "${CMAKE_CURRENT_SOURCE_DIR}/native/webscene_v8_runtime.cpp"
                --output "${WEBSCENE_V8_BOOTSTRAP_SOURCE}"
            RESULT_VARIABLE WEBSCENE_V8_BOOTSTRAP_EXTRACT_RESULT)
        if(NOT WEBSCENE_V8_BOOTSTRAP_EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Unable to extract WebScene V8 bootstrap sources")
        endif()
        file(SHA256 "${WEBSCENE_V8_BOOTSTRAP_SOURCE}"
            WEBSCENE_V8_BOOTSTRAP_SOURCE_SHA256)
        file(SHA256
            "${CMAKE_CURRENT_SOURCE_DIR}/native/generated/webscene_dom_bindings.inc"
            WEBSCENE_V8_BINDING_CATALOG_SHA256)
        if(WEBSCENE_NATIVE_ENGINE_ENABLE_WEBGPU)
            file(SHA256
                "${CMAKE_CURRENT_SOURCE_DIR}/native/webscene_v8_runtime_webgpu.inc"
                WEBSCENE_V8_WEBGPU_BINDINGS_SHA256)
        else()
            set(WEBSCENE_V8_WEBGPU_BINDINGS_SHA256 "disabled")
        endif()
        file(SHA256 "${WEBSCENE_V8_ROOT}/include/v8-version.h"
            WEBSCENE_V8_VERSION_HEADER_SHA256)
        string(JOIN "," WEBSCENE_V8_SNAPSHOT_FINGERPRINT
            "v8_version_header=${WEBSCENE_V8_VERSION_HEADER_SHA256}"
            "cpu=${WEBSCENE_V8_CPU}"
            "pointer_compression=${WEBSCENE_V8_POINTER_COMPRESSION}"
            "shared_cage=${WEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE}"
            "direct_handle=${WEBSCENE_V8_DIRECT_HANDLE}"
            "optimize_for_size=${WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT}"
            "bootstrap=${WEBSCENE_V8_BOOTSTRAP_SOURCE_SHA256}"
            "bindings=${WEBSCENE_V8_BINDING_CATALOG_SHA256}"
            "webgpu=${WEBSCENE_V8_WEBGPU_BINDINGS_SHA256}")
        string(REPLACE ";" "," WEBSCENE_V8_SNAPSHOT_FINGERPRINT
            "${WEBSCENE_V8_SNAPSHOT_FINGERPRINT}")

        add_executable(webscene_v8_snapshot_builder
            tools/v8_snapshot_builder.cpp)
        target_compile_features(webscene_v8_snapshot_builder PRIVATE cxx_std_20)
        target_include_directories(webscene_v8_snapshot_builder SYSTEM PRIVATE
            "${WEBSCENE_V8_ROOT}/include")
        target_compile_definitions(webscene_v8_snapshot_builder PRIVATE
            WEBSCENE_V8_SNAPSHOT_FINGERPRINT="${WEBSCENE_V8_SNAPSHOT_FINGERPRINT}")
        if(WEBSCENE_V8_POINTER_COMPRESSION)
            target_compile_definitions(webscene_v8_snapshot_builder PRIVATE
                V8_COMPRESS_POINTERS V8_31BIT_SMIS_ON_64BIT_ARCH)
        endif()
        if(WEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE)
            target_compile_definitions(webscene_v8_snapshot_builder PRIVATE
                V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
        endif()
        if(WEBSCENE_V8_DIRECT_HANDLE)
            target_compile_definitions(webscene_v8_snapshot_builder PRIVATE
                V8_ENABLE_DIRECT_HANDLE)
        endif()
        target_link_libraries(webscene_v8_snapshot_builder PRIVATE
            "${WEBSCENE_V8_MONOLITH}")
        if(APPLE)
            target_link_libraries(webscene_v8_snapshot_builder PRIVATE
                "-framework CoreFoundation")
        elseif(WIN32)
            target_link_libraries(webscene_v8_snapshot_builder PRIVATE
                dbghelp winmm advapi32)
        elseif(UNIX)
            target_link_libraries(webscene_v8_snapshot_builder PRIVATE dl rt)
        endif()

        set(WEBSCENE_V8_SNAPSHOT_BLOB
            "${CMAKE_CURRENT_BINARY_DIR}/webscene_bootstrap_snapshot.bin")
        set(WEBSCENE_V8_SNAPSHOT_METADATA
            "${CMAKE_CURRENT_BINARY_DIR}/webscene_bootstrap_snapshot.meta")
        add_custom_command(
            OUTPUT "${WEBSCENE_V8_SNAPSHOT_BLOB}" "${WEBSCENE_V8_SNAPSHOT_METADATA}"
            COMMAND "$<TARGET_FILE:webscene_v8_snapshot_builder>"
                "${WEBSCENE_V8_ICU_DATA}"
                "${WEBSCENE_V8_BOOTSTRAP_SOURCE}"
                "${WEBSCENE_V8_SNAPSHOT_BLOB}"
                "${WEBSCENE_V8_SNAPSHOT_METADATA}"
            DEPENDS webscene_v8_snapshot_builder "${WEBSCENE_V8_BOOTSTRAP_SOURCE}"
            COMMENT "Creating the WebScene V8 bootstrap snapshot"
            VERBATIM)
        add_custom_target(webscene_v8_bootstrap_snapshot ALL
            DEPENDS "${WEBSCENE_V8_SNAPSHOT_BLOB}" "${WEBSCENE_V8_SNAPSHOT_METADATA}")
        add_dependencies(webscene_native_engine webscene_v8_bootstrap_snapshot)
        target_compile_definitions(webscene_native_engine PRIVATE
            WEBSCENE_V8_BOOTSTRAP_SNAPSHOT=1
            WEBSCENE_V8_SNAPSHOT_FILENAME="webscene_bootstrap_snapshot.bin"
            WEBSCENE_V8_SNAPSHOT_METADATA_FILENAME="webscene_bootstrap_snapshot.meta"
            WEBSCENE_V8_SNAPSHOT_FINGERPRINT="${WEBSCENE_V8_SNAPSHOT_FINGERPRINT}")
        message(STATUS "WebScene native engine: bootstrap snapshot selected")
    elseif(NOT WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT STREQUAL "none")
        message(FATAL_ERROR
            "WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT must be none or bootstrap")
    else()
        message(STATUS "WebScene native engine: startup snapshot disabled")
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
        if(WEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR)
            target_compile_definitions(webscene_native_engine_tests PRIVATE
                WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR=1)
        endif()
        target_link_libraries(webscene_native_engine_tests PRIVATE
            webscene_native_engine
            ixwebsocket)
        add_test(NAME webscene_native_engine_tests COMMAND webscene_native_engine_tests)
        set_tests_properties(webscene_native_engine_tests PROPERTIES
            ENVIRONMENT
                "WEBSCENE_V8_DETAILED_MEMORY_METRICS=1;WEBSCENE_INTEROP_STRESS=1")
        if(WEBSCENE_NATIVE_ENGINE_ENABLE_WEBGPU)
            add_executable(webscene_native_webgpu_triangle_tests
                tests/native_webgpu_triangle_tests.cpp)
            target_compile_features(webscene_native_webgpu_triangle_tests PRIVATE cxx_std_20)
            target_link_libraries(webscene_native_webgpu_triangle_tests PRIVATE
                webscene_native_engine)
            if(EXISTS "${WEBSCENE_GPU_PROVIDER_TEST_LIBRARY}")
                add_test(NAME webscene_native_webgpu_triangle_tests
                    COMMAND webscene_native_webgpu_triangle_tests
                        "${WEBSCENE_GPU_PROVIDER_TEST_LIBRARY}")
                if(EXISTS "${WEBSCENE_THREE_WEBGPU_TEST_SCRIPT}")
                    add_test(NAME webscene_native_webgpu_three_tests
                        COMMAND webscene_native_webgpu_triangle_tests
                            "${WEBSCENE_GPU_PROVIDER_TEST_LIBRARY}"
                            "${WEBSCENE_THREE_WEBGPU_TEST_SCRIPT}")
                    if(EXISTS "${WEBSCENE_WEBGPU_CTS_TEST_SCRIPT}")
                        add_test(NAME webscene_native_webgpu_cts_tests
                            COMMAND webscene_native_webgpu_triangle_tests
                                "${WEBSCENE_GPU_PROVIDER_TEST_LIBRARY}"
                                "${WEBSCENE_THREE_WEBGPU_TEST_SCRIPT}"
                                "${WEBSCENE_WEBGPU_CTS_TEST_SCRIPT}")
                    endif()
                endif()
            endif()
        endif()
    endif()
    if(WEBSCENE_NATIVE_ENGINE_BUILD_DOM_BINDING_BENCHMARK)
        add_executable(webscene_dom_binding_benchmark
            benchmarks/dom_binding_benchmark.cpp)
        target_compile_features(webscene_dom_binding_benchmark PRIVATE cxx_std_20)
        target_link_libraries(webscene_dom_binding_benchmark PRIVATE
            webscene_native_engine)
    endif()
else()
    message(STATUS "WebScene native engine: V8 intentionally disabled")
endif()

if(BUILD_TESTING)
    add_library(webscene_mock_gpu_provider SHARED
        tests/mock_gpu_provider.cpp)
    target_compile_features(webscene_mock_gpu_provider PRIVATE cxx_std_20)
    target_include_directories(webscene_mock_gpu_provider PRIVATE native)

    add_executable(webscene_gpu_provider_contract_tests
        tests/gpu_provider_contract_tests.cpp)
    target_compile_features(webscene_gpu_provider_contract_tests PRIVATE cxx_std_20)
    target_include_directories(webscene_gpu_provider_contract_tests PRIVATE native)
    target_link_libraries(webscene_gpu_provider_contract_tests PRIVATE
        webscene_native_engine)
    add_test(
        NAME webscene_gpu_provider_contract_tests
        COMMAND webscene_gpu_provider_contract_tests
            $<TARGET_FILE:webscene_mock_gpu_provider>)
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
    target_compile_definitions(webscene_native_engine PRIVATE NOMINMAX)
    if(TARGET webscene_native_engine_tests)
        set_property(TARGET webscene_native_engine_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(webscene_native_engine_tests PRIVATE
            /std:c++20 /Zc:__cplusplus)
        target_compile_definitions(webscene_native_engine_tests PRIVATE NOMINMAX)
    endif()
    if(TARGET webscene_v8_snapshot_builder)
        target_compile_options(webscene_v8_snapshot_builder PRIVATE
            /std:c++20 /Zc:__cplusplus)
    endif()
    if(TARGET webscene_html_parser_tests)
        set_property(TARGET webscene_html_parser_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(webscene_html_parser_tests PRIVATE
            /std:c++20 /Zc:__cplusplus)
        target_compile_definitions(webscene_html_parser_tests PRIVATE NOMINMAX)
    endif()
    if(TARGET webscene_html_parser_benchmark)
        set_property(TARGET webscene_html_parser_benchmark PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(webscene_html_parser_benchmark PRIVATE
            /std:c++20 /Zc:__cplusplus)
        target_compile_definitions(webscene_html_parser_benchmark PRIVATE NOMINMAX)
    endif()
else()
    target_compile_options(webscene_native_engine PRIVATE
        -Wall -Wextra -Wpedantic -fno-rtti -Wno-deprecated-declarations)
    if(TARGET webscene_html_parser_tests)
        target_compile_options(webscene_html_parser_tests PRIVATE
            -Wall -Wextra -Wpedantic -fno-rtti)
    endif()
    if(TARGET webscene_html_parser_benchmark)
        target_compile_options(webscene_html_parser_benchmark PRIVATE
            -Wall -Wextra -Wpedantic -fno-rtti)
    endif()
endif()

if(WEBSCENE_NATIVE_ENGINE_THIN_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT WEBSCENE_THIN_LTO_SUPPORTED
        OUTPUT WEBSCENE_THIN_LTO_ERROR
        LANGUAGES C CXX)
    if(NOT WEBSCENE_THIN_LTO_SUPPORTED)
        message(FATAL_ERROR
            "WebScene native engine: ThinLTO is unavailable: "
            "${WEBSCENE_THIN_LTO_ERROR}")
    endif()
    set_property(TARGET webscene_native_engine PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Chromium's ThinLTO V8 archive uses split LTO units and whole-program
        # vtable optimization. Every bitcode input in the final link must use
        # the same unit-splitting mode.
        target_compile_options(webscene_native_engine PRIVATE
            -fsplit-lto-unit
            -fwhole-program-vtables)
        target_link_options(webscene_native_engine PRIVATE
            -fsplit-lto-unit
            -fwhole-program-vtables)
    endif()
    message(STATUS "WebScene native engine: ThinLTO enabled")
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
