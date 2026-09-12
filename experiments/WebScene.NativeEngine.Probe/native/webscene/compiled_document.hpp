#pragma once
#include "webscene_native_engine.h"
#include "webscene_compiled_document.h"
#include <string_view>

// C linkage keeps the registration symbol available in shared builds too.
// The pointed-to package is C++; callers use the SDK's qualified C++ ABI.
extern "C" WEBSCENE_API uint8_t webscene_engine_register_compiled_document_v1(
    webscene_engine*, const char* name, size_t name_length,
    const webscene_native::compiled_document* package);

// Optional host-level cache root shared with the engine's existing JavaScript
// compilation cache. CSS syntax units stored under this root are content-checked
// before replay and contain no document/cascade state.
extern "C" WEBSCENE_API void webscene_css_set_compilation_cache_directory_v1(
    const char* directory, size_t directory_length);

namespace webscene {
using compiled_document = webscene_native::compiled_document;
// Registers a package on this engine only. Registration owns a copy; each load
// snapshots it before queuing work. Replacing a name affects subsequent loads.
// Construction and template callbacks run on the engine's document owner thread.
// The application must keep code containing those callbacks loaded until engine
// destruction. No calls, including registration, may race engine destruction.
inline bool register_compiled_document(webscene_engine* engine, std::string_view name,
                                       const compiled_document& package) {
    return webscene_engine_register_compiled_document_v1(engine,name.data(),name.size(),&package)!=0;
}
}
