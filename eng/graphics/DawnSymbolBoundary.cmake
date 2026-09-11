# Loaded through CMAKE_PROJECT_Dawn_INCLUDE. Keep Dawn's bundled dependencies
# out of V8's symbol namespace without modifying the pinned upstream source.
function(webscene_isolate_dawn)
    if(NOT TARGET webgpu_dawn OR NOT DAWN_BUILD_MONOLITHIC_LIBRARY STREQUAL "SHARED")
        message(FATAL_ERROR "WebScene requires the shared Dawn monolith")
    endif()
    foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(definitions webgpu_dawn_objects ${property})
        if(definitions)
            list(REMOVE_ITEM definitions DAWN_NATIVE_SHARED_LIBRARY)
            set_property(TARGET webgpu_dawn_objects PROPERTY ${property} "${definitions}")
        endif()
    endforeach()
    if(APPLE)
        file(WRITE "${CMAKE_BINARY_DIR}/webscene-dawn.exports" "_wgpu*\n")
        target_link_options(webgpu_dawn PRIVATE "LINKER:-exported_symbols_list,${CMAKE_BINARY_DIR}/webscene-dawn.exports")
    elseif(UNIX)
        file(WRITE "${CMAKE_BINARY_DIR}/webscene-dawn.exports" "{ global: wgpu*; local: *; };\n")
        target_link_options(webgpu_dawn PRIVATE "LINKER:--version-script=${CMAKE_BINARY_DIR}/webscene-dawn.exports")
    endif()
    # Windows exports only functions decorated by WGPU_SHARED_LIBRARY;
    # removing DAWN_NATIVE_SHARED_LIBRARY above excludes the C++ native API.
endfunction()
cmake_language(DEFER CALL webscene_isolate_dawn)
