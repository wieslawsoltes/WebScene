# A patched header paired with an older archive changes V8Inspector's vtable
# layout. Header-only validation accepts that pair, but later virtual calls
# dispatch to unrelated functions. Require the patch's compiled implementation.
file(READ "${WEBSCENE_V8_ROOT}/include/v8-inspector.h" inspector_header)
if(NOT inspector_header MATCHES "virtual void consoleAPICalled")
    message(FATAL_ERROR "V8 Inspector requires WebScene's console bridge patch in both headers and monolith. Rebuild the V8 SDK.")
endif()
set(inspector_llvm_nm "${WEBSCENE_V8_ROOT}/third_party/llvm-build/Release+Asserts/bin/llvm-nm")
if(WIN32)
    string(APPEND inspector_llvm_nm ".exe")
endif()
if(EXISTS "${inspector_llvm_nm}")
    # Match the archive's LLVM version, including ThinLTO bitcode members.
    set(inspector_symbol_tool "${inspector_llvm_nm}")
    set(inspector_symbol_args -g --defined-only)
elseif(MSVC)
    get_filename_component(inspector_linker_directory "${CMAKE_LINKER}" DIRECTORY)
    find_program(inspector_symbol_tool NAMES dumpbin HINTS "${inspector_linker_directory}" REQUIRED)
    set(inspector_symbol_args /symbols)
elseif(APPLE)
    set(inspector_symbol_tool "${CMAKE_NM}")
    set(inspector_symbol_args -g -U)
else()
    set(inspector_symbol_tool "${CMAKE_NM}")
    set(inspector_symbol_args -g --defined-only)
endif()
execute_process(
    COMMAND "${inspector_symbol_tool}" ${inspector_symbol_args} "${WEBSCENE_V8_MONOLITH}"
    RESULT_VARIABLE inspector_symbols_status
    OUTPUT_VARIABLE inspector_symbols
    ERROR_VARIABLE inspector_symbols_error
    TIMEOUT 120)
if(NOT inspector_symbols_status STREQUAL "0")
    message(FATAL_ERROR "Cannot verify the V8 Inspector archive ABI: ${inspector_symbols_error}")
endif()
# Itanium and MSVC put the class/method names in opposite orders. On MSVC,
# exclude UNDEF records: a reference does not prove the implementation exists.
# Anchor each attempt at a line boundary. dumpbin emits a very large symbol
# table on Windows; an unanchored leading wildcard rescans each long line.
string(REGEX MATCHALL "(^|\n)[^\n]*V8InspectorImpl[^\n]*" inspector_symbol_lines "${inspector_symbols}")
set(inspector_bridge_found OFF)
foreach(symbol_line IN LISTS inspector_symbol_lines)
    if(symbol_line MATCHES "consoleAPICalled" AND NOT symbol_line MATCHES "UNDEF")
        set(inspector_bridge_found ON)
    endif()
endforeach()
if(NOT inspector_bridge_found)
    message(FATAL_ERROR "V8 Inspector header/archive ABI mismatch: the header declares the console bridge but the monolith has no V8InspectorImpl::consoleAPICalled implementation. Rebuild the monolith after applying V8InspectorConsolePatch.txt; do not reuse an older archive.")
endif()
unset(inspector_symbols)
unset(inspector_symbol_lines)
