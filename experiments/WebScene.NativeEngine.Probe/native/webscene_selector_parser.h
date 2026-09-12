#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace webscene_native {

struct selector_syntax_selector final {
    std::string serialized;
    uint32_t specificity{0};
    std::vector<std::string> compounds;
    std::vector<char> combinators;
};

struct selector_syntax_metrics final {
    uint64_t duration_ns{0};
    uint64_t rust_allocation_count{0};
    uint64_t rust_peak_bytes{0};
    uint64_t rust_retained_bytes{0};
    bool compilation_cache_hit{false};
};

struct selector_syntax_output final {
    std::vector<selector_syntax_selector> selectors;
    selector_syntax_metrics metrics;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

void set_selector_syntax_compilation_cache_directory(std::string directory);
void clear_selector_syntax_process_cache();
uint64_t selector_syntax_process_cache_hits() noexcept;
uint64_t selector_syntax_persistent_cache_hits() noexcept;
uint64_t selector_syntax_compilation_count() noexcept;

selector_syntax_output parse_selector_syntax(std::string_view input);

} // namespace webscene_native
