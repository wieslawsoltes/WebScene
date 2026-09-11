include(CMakeParseArguments)
function(webscene_compile_html target input)
  cmake_parse_arguments(UI "" "MODULE;NAMESPACE;CSS_BACKEND;MODE" "OPTIONS;DEPENDS" ${ARGN})
  get_filename_component(input "${input}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(arguments)
  if(UI_MODULE)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${target}_ui.cppm")
    list(APPEND arguments --module "${UI_MODULE}")
  else()
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${target}_ui.hpp")
  endif()
  foreach(name NAMESPACE CSS_BACKEND)
    if(UI_${name})
      string(TOLOWER "${name}" flag)
      string(REPLACE "_" "-" flag "${flag}")
      list(APPEND arguments "--${flag}" "${UI_${name}}")
    endif()
  endforeach()
  list(APPEND arguments ${UI_OPTIONS})
  set(prefix)
  if(UI_MODE STREQUAL "hybrid")
    if(NOT UI_MODULE)
      message(FATAL_ERROR "Hybrid UI requires MODULE")
    endif()
    set(prefix --engine-module)
  elseif(UI_MODE AND NOT UI_MODE STREQUAL "native")
    message(FATAL_ERROR "MODE must be native or hybrid")
  endif()
  add_custom_command(OUTPUT "${output}"
    COMMAND WebScene::Compiler ${prefix} "${input}" "${output}" ${arguments}
    DEPENDS WebScene::Compiler "${input}" ${UI_DEPENDS}
    DEPFILE "${output}.d" VERBATIM)
  if(UI_MODULE)
    target_sources(${target} PRIVATE FILE_SET webscene_ui TYPE CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}" FILES "${output}")
  else()
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endfunction()
function(webscene_prepare_css target input module_name)
  get_filename_component(input "${input}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/${target}_css.cppm")
  add_custom_command(OUTPUT "${output}" COMMAND WebScene::Compiler --prepare-css "${input}"
    "${output}" --module "${module_name}" DEPENDS WebScene::Compiler "${input}" VERBATIM)
  target_sources(${target} PRIVATE FILE_SET webscene_css TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}" FILES "${output}")
endfunction()
# Console consumers keep the selected runtime data beside their executable.
function(webscene_stage_runtime target)
  file(GLOB runtime_assets "${WebScene_SDK_ROOT}/share/webscene/runtime/*")
  set_target_properties(${target} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${runtime_assets}
      "${WebScene_SDK_ROOT}/lib/libwebgpu_dawn.dylib" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
endfunction()
# Compile a small application-side registrar; the reusable runtime is untouched.
function(webscene_register_compiled_package target)
  cmake_parse_arguments(PACKAGE "" "MODULE;NAME;FUNCTION" "" ${ARGN})
  if(NOT PACKAGE_MODULE MATCHES "^[A-Za-z_][A-Za-z0-9_.]*$" OR
     NOT PACKAGE_NAME MATCHES "^[A-Za-z0-9_.-]+$" OR
     NOT PACKAGE_FUNCTION MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "Registration requires MODULE, NAME and a C++ FUNCTION identifier")
  endif()
  set(header "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_FUNCTION}.hpp")
  set(source "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_FUNCTION}.cpp")
  file(GENERATE OUTPUT "${header}" CONTENT
    "#pragma once\n#include <webscene_native_engine.h>\nbool ${PACKAGE_FUNCTION}(webscene_engine*);\n")
  file(GENERATE OUTPUT "${source}" CONTENT
    "#include <webscene/compiled_document.hpp>\nimport ${PACKAGE_MODULE};\nbool ${PACKAGE_FUNCTION}(webscene_engine* engine) { return webscene::register_compiled_document(engine,\"${PACKAGE_NAME}\",compiled_engine::build(\"\")); }\n")
  target_sources(${target} PRIVATE "${source}")
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()
