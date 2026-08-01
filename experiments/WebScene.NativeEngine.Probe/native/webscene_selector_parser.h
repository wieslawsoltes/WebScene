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
};

struct selector_syntax_output final {
    std::vector<selector_syntax_selector> selectors;
    selector_syntax_metrics metrics;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

selector_syntax_output parse_selector_syntax(std::string_view input);

} // namespace webscene_native
