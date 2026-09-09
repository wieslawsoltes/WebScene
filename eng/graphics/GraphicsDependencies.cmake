# Import only SDKs produced together with the checked-in graphics dependency lock.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64|arm64")
    set(WEBSCENE_GRAPHICS_RID win-x64)
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(WEBSCENE_GRAPHICS_RID osx-arm64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(WEBSCENE_GRAPHICS_RID linux-x64)
else()
    message(FATAL_ERROR "No graphics dependency profile for this architecture/platform")
endif()

set(WEBSCENE_GRAPHICS_SDK_ROOT "" CACHE PATH "Directory containing dawn/ and angle/ SDKs for this RID")
set(WEBSCENE_GRAPHICS_ANGLE_VARIANT "angle" CACHE STRING "ANGLE SDK: angle or separately built angle-gl")
set_property(CACHE WEBSCENE_GRAPHICS_ANGLE_VARIANT PROPERTY STRINGS angle angle-gl)
if(NOT WEBSCENE_GRAPHICS_ANGLE_VARIANT MATCHES "^angle(-gl)?$")
    message(FATAL_ERROR "Unknown ANGLE SDK variant")
endif()
if(NOT WEBSCENE_GRAPHICS_SDK_ROOT)
    message(FATAL_ERROR "Set WEBSCENE_GRAPHICS_SDK_ROOT to the output RID directory from eng/graphics/build.py")
endif()

foreach(component IN LISTS WEBSCENE_GRAPHICS_COMPONENTS)
    set(sdk "${WEBSCENE_GRAPHICS_SDK_ROOT}/${component}")
    if(component STREQUAL "angle")
        set(sdk "${WEBSCENE_GRAPHICS_SDK_ROOT}/${WEBSCENE_GRAPHICS_ANGLE_VARIANT}")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify-sdk.py"
            "${sdk}" --component "${component}" --rid "${WEBSCENE_GRAPHICS_RID}"
        RESULT_VARIABLE verification_result
        OUTPUT_VARIABLE verification_output ERROR_VARIABLE verification_error)
    if(NOT verification_result EQUAL 0)
        message(FATAL_ERROR "${verification_output}${verification_error}")
    endif()
    message(STATUS "${verification_output}")
    if(component STREQUAL "dawn")
        find_package(Dawn CONFIG REQUIRED PATHS "${sdk}/lib/cmake/Dawn" NO_DEFAULT_PATH)
        if(WIN32)
            target_compile_definitions(dawn::webgpu_dawn INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        # CMake's cache may retain Dawn_DIR from an unrelated install. Reject that too.
        file(REAL_PATH "${Dawn_DIR}" resolved_dawn)
        file(REAL_PATH "${sdk}/lib/cmake/Dawn" expected_dawn)
        if(NOT resolved_dawn STREQUAL expected_dawn)
            message(FATAL_ERROR "Dawn_DIR points outside the verified SDK; clear the stale cache")
        endif()
    elseif(component STREQUAL "angle")
        foreach(library EGL GLESv2)
            add_library(webscene_angle_${library} SHARED IMPORTED GLOBAL)
            set_target_properties(webscene_angle_${library} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${sdk}/include")
            if(WIN32)
                target_compile_definitions(webscene_angle_${library} INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
                set_target_properties(webscene_angle_${library} PROPERTIES
                    IMPORTED_LOCATION "${sdk}/lib/lib${library}.dll"
                    IMPORTED_IMPLIB "${sdk}/lib/lib${library}.lib")
            else()
                set_target_properties(webscene_angle_${library} PROPERTIES
                    IMPORTED_LOCATION "${sdk}/lib/lib${library}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            endif()
        endforeach()
    else()
        message(FATAL_ERROR "Unknown graphics component: ${component}")
    endif()
endforeach()
