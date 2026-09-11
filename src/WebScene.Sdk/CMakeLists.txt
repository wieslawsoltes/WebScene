cmake_minimum_required(VERSION 3.28)
project(WebSceneSDK VERSION 0.1.0 LANGUAGES C CXX)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(cmake/LinuxSDK.cmake)
  return()
endif()
enable_language(OBJCXX)
if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
 message(FATAL_ERROR "The SDK preview qualifies macOS ARM64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_TESTING OFF CACHE BOOL "SDK tests consume the installed package separately" FORCE)
set(WEBSCENE_NATIVE_WEB_SDK_BUILD ON)
set(WEBSCENE_NATIVE_ENGINE_STATIC ON CACHE BOOL "" FORCE)
set(WEBSCENE_COMPILED_APPLICATION_MODULE "" CACHE FILEPATH "SDK runtime never contains application code" FORCE)
set(WEBSCENE_NATIVE_ENGINE_ENABLE_V8 ON CACHE BOOL "" FORCE)
set(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS ON CACHE BOOL "" FORCE)
get_filename_component(WEBSCENE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${WEBSCENE_ROOT}/src/WebScene.NativeWeb" native-web)
# Resolve graphics at producer scope so public source-build targets are usable.
set(WEBSCENE_GRAPHICS_COMPONENTS dawn)
include("${WEBSCENE_ROOT}/eng/graphics/GraphicsDependencies.cmake")
add_subdirectory("${WEBSCENE_ROOT}/experiments/WebScene.NativeEngine.Probe" runtime)
add_library(WebScene::Core ALIAS webscene_core)
add_library(WebScene::NativeWeb ALIAS webscene_native_web)
add_library(WebScene::Runtime ALIAS webscene_native_engine)
add_executable(WebScene::Compiler ALIAS webscene-uic)
add_library(webscene_sdk_webgpu INTERFACE)
target_include_directories(webscene_sdk_webgpu INTERFACE
 "${WEBSCENE_ROOT}/experiments/WebScene.NativeEngine.Probe/native/graphics")
target_link_libraries(webscene_sdk_webgpu INTERFACE webscene_core dawn::webgpu_dawn)
add_library(WebScene::WebGPU ALIAS webscene_sdk_webgpu)
# Preserve resolved dependency paths for the SDK packager, never for consumers.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/sdk-libraries.txt" CONTENT
 "$<TARGET_FILE:webscene_core>\n$<TARGET_FILE:webscene_native_web>\n$<TARGET_FILE:webscene_native_web_shared_css>\n$<TARGET_FILE:webscene_native_engine>\n$<TARGET_FILE:webscene_media>\n$<TARGET_FILE:ixwebsocket>\n")

include(GNUInstallDirs)
install(TARGETS webscene_core webscene_native_web webscene_native_web_shared_css
  webscene_native_engine webscene_media ixwebsocket ARCHIVE DESTINATION lib)
install(TARGETS webscene-uic RUNTIME DESTINATION bin)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${WEBSCENE_ROOT}/eng/sdk/header-closure.py" "${WEBSCENE_ROOT}"
  OUTPUT_FILE "${CMAKE_BINARY_DIR}/sdk-headers.cmake" COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_BINARY_DIR}/sdk-headers.cmake")
install(FILES "${CMAKE_BINARY_DIR}/native-web/parser/release/libwebscene_html_parser.a"
  "${WEBSCENE_V8_OUTPUT_ROOT}/obj/libv8_monolith.a" DESTINATION lib)
install(DIRECTORY "${WEBSCENE_GRAPHICS_SDK_ROOT}/dawn/include/" DESTINATION include)
install(FILES "${WEBSCENE_GRAPHICS_SDK_ROOT}/dawn/lib/libwebgpu_dawn.dylib" DESTINATION lib)
install(FILES "${WEBSCENE_V8_OUTPUT_ROOT}/icudtl.dat"
  "${CMAKE_BINARY_DIR}/runtime/webscene_bootstrap_snapshot.bin"
  "${CMAKE_BINARY_DIR}/runtime/webscene_bootstrap_snapshot.meta" DESTINATION share/webscene/runtime)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" DESTINATION lib/cmake/WebScene)
install(FILES "${WEBSCENE_V8_ROOT}/LICENSE" DESTINATION share/licenses/WebScene RENAME V8-LICENSE)
install(FILES "${CMAKE_BINARY_DIR}/webscene-miniaudio-LICENSE" DESTINATION share/licenses/WebScene)
install(FILES "${CMAKE_BINARY_DIR}/_deps/webscene_ixwebsocket-src/LICENSE.txt"
 DESTINATION share/licenses/WebScene RENAME IXWebSocket-LICENSE)
install(PROGRAMS tools/package_macos.py DESTINATION share/webscene/tools)

install(DIRECTORY "${WEBSCENE_GRAPHICS_SDK_ROOT}/dawn/licenses/" DESTINATION share/licenses/WebScene/Dawn)
install(FILES "${WEBSCENE_GRAPHICS_SDK_ROOT}/dawn/webscene-graphics-package.json"
  "${WEBSCENE_ROOT}/eng/graphics/dependencies.lock.json" DESTINATION share/webscene/dependencies)
include(CMakePackageConfigHelpers)
write_basic_package_version_file("${CMAKE_BINARY_DIR}/WebSceneConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_BINARY_DIR}/WebSceneConfigVersion.cmake" DESTINATION lib/cmake/WebScene)
