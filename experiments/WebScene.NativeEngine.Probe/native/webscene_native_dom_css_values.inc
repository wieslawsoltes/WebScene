css_length native_document::parse_length(const std::string& value)
{
    if (value.empty() || value == "auto") {
        return {};
    }
    if (value.starts_with("calc(") || value.starts_with("min(") || value.starts_with("max(")) {
        if (const auto calculated = calc_parser(value).parse(); calculated.has_value()) {
            return {
                static_cast<float>(calculated->value),
                calculated->unit == calc_unit::percent
                    ? length_unit::percent
                    : length_unit::pixels,
                static_cast<float>(calculated->pixel_offset)};
        }
        return {};
    }
    if (value == "max-content") return {0, length_unit::max_content};
    if (value == "min-content") return {0, length_unit::min_content};
    if (value == "fit-content") return {0, length_unit::fit_content};
    if (value.size() > 1 && value.back() == '%') {
        return {parse_number(std::string_view(value).substr(0, value.size() - 1)), length_unit::percent};
    }
    auto length = value.size();
    auto unit = length_unit::pixels;
    auto multiplier = 1.0F;
    if (length >= 3 && value.ends_with("rem")) {
        length -= 3;
        unit = length_unit::rem;
    } else if (length >= 2 && value.ends_with("em")) {
        length -= 2;
        unit = length_unit::em;
    } else if (length >= 2 && value.ends_with("vw")) {
        length -= 2;
        unit = length_unit::viewport_width;
    } else if (length >= 2 && value.ends_with("vh")) {
        length -= 2;
        unit = length_unit::viewport_height;
    } else if (length >= 2 && value.ends_with("in")) {
        length -= 2;
        multiplier = 96.0F;
    } else if (length >= 2 && value.ends_with("cm")) {
        length -= 2;
        multiplier = 96.0F / 2.54F;
    } else if (length >= 2 && value.ends_with("mm")) {
        length -= 2;
        multiplier = 96.0F / 25.4F;
    } else if (length >= 2 && value.ends_with("pt")) {
        length -= 2;
        multiplier = 96.0F / 72.0F;
    } else if (length >= 2 && value.ends_with("pc")) {
        length -= 2;
        multiplier = 16.0F;
    } else if (length >= 1 && value.ends_with("q")) {
        length -= 1;
        multiplier = 96.0F / 101.6F;
    } else if (length >= 2 && value[length - 2] == 'p' && value[length - 1] == 'x') {
        length -= 2;
    }
    return {
        parse_number(std::string_view(value).substr(0, length)) * multiplier,
        unit};
}

void native_document::parse_transform_translate(
    const std::string& value,
    css_length& translate_x,
    css_length& translate_y,
    float& scale_x,
    float& scale_y,
    float& rotate_degrees)
{
    translate_x = {};
    translate_y = {};
    scale_x = 1;
    scale_y = 1;
    rotate_degrees = 0;
    if (value.empty() || value == "none") return;

    const auto parse_arguments = [&](std::string_view function) {
        const auto open = value.find(std::string(function) + "(");
        if (open == std::string::npos) return std::vector<std::string>{};
        const auto start = open + function.size() + 1U;
        const auto close = value.find(')', start);
        if (close == std::string::npos) return std::vector<std::string>{};
        auto arguments = value.substr(start, close - start);
        std::replace(arguments.begin(), arguments.end(), ',', ' ');
        std::istringstream stream(arguments);
        std::vector<std::string> result;
        for (std::string argument; stream >> argument;) result.push_back(std::move(argument));
        return result;
    };

    if (const auto arguments = parse_arguments("translate"); !arguments.empty()) {
        translate_x = parse_length(arguments[0]);
        translate_y = arguments.size() > 1 ? parse_length(arguments[1]) : css_length{};
    } else {
        if (const auto arguments = parse_arguments("translateX"); !arguments.empty()) {
            translate_x = parse_length(arguments[0]);
        }
        if (const auto arguments = parse_arguments("translateY"); !arguments.empty()) {
            translate_y = parse_length(arguments[0]);
        }
    }

    if (const auto arguments = parse_arguments("scale"); !arguments.empty()) {
        scale_x = parse_number(arguments[0]);
        scale_y = arguments.size() > 1 ? parse_number(arguments[1]) : scale_x;
    } else {
        if (const auto arguments = parse_arguments("scaleX"); !arguments.empty()) {
            scale_x = parse_number(arguments[0]);
        }
        if (const auto arguments = parse_arguments("scaleY"); !arguments.empty()) {
            scale_y = parse_number(arguments[0]);
        }
    }

    if (const auto arguments = parse_arguments("rotate"); !arguments.empty()) {
        auto angle = arguments[0];
        if (angle.ends_with("deg")) angle.resize(angle.size() - 3U);
        else if (angle.ends_with("turn")) {
            angle.resize(angle.size() - 4U);
            rotate_degrees = parse_number(angle) * 360.0F;
            return;
        }
        rotate_degrees = parse_number(angle);
    }
}

void native_document::parse_transform_origin(
    const std::string& value,
    css_length& origin_x,
    css_length& origin_y)
{
    auto normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::istringstream stream(normalized);
    std::string first;
    std::string second;
    stream >> first >> second;

    const auto horizontal = [](const std::string& token) {
        if (token == "left") return css_length{0, length_unit::percent};
        if (token == "center") return css_length{50, length_unit::percent};
        if (token == "right") return css_length{100, length_unit::percent};
        return parse_length(token);
    };
    const auto vertical = [](const std::string& token) {
        if (token == "top") return css_length{0, length_unit::percent};
        if (token == "center") return css_length{50, length_unit::percent};
        if (token == "bottom") return css_length{100, length_unit::percent};
        return parse_length(token);
    };
    const auto is_vertical_keyword = [](const std::string& token) {
        return token == "top" || token == "bottom";
    };
    const auto is_horizontal_keyword = [](const std::string& token) {
        return token == "left" || token == "right";
    };

    if (first.empty()) {
        origin_x = {50, length_unit::percent};
        origin_y = {50, length_unit::percent};
    } else if (second.empty()) {
        if (is_vertical_keyword(first)) {
            origin_x = {50, length_unit::percent};
            origin_y = vertical(first);
        } else {
            origin_x = horizontal(first);
            origin_y = {50, length_unit::percent};
        }
    } else if (is_vertical_keyword(first) && is_horizontal_keyword(second)) {
        origin_x = horizontal(second);
        origin_y = vertical(first);
    } else {
        origin_x = horizontal(first);
        origin_y = vertical(second);
    }
}

uint32_t native_document::parse_color(const std::string& value)
{
    if (value == "transparent" || value.empty()) return 0;
    if (value == "black") return 0x000000FFU;
    if (value == "white") return 0xFFFFFFFFU;
    if (value == "red") return 0xFF0000FFU;
    if (value == "green") return 0x008000FFU;
    if (value == "lime") return 0x00FF00FFU;
    if (value == "blue") return 0x0000FFFFU;
    if (value == "yellow") return 0xFFFF00FFU;
    if (value == "cyan" || value == "aqua") return 0x00FFFFFFU;
    if (value == "magenta" || value == "fuchsia") return 0xFF00FFFFU;
    if (value == "gray" || value == "grey") return 0x808080FFU;
    if (value == "silver") return 0xC0C0C0FFU;
    if (value == "maroon") return 0x800000FFU;
    if (value == "purple") return 0x800080FFU;
    if (value == "olive") return 0x808000FFU;
    if (value == "navy") return 0x000080FFU;
    if (value == "teal") return 0x008080FFU;
    if (value == "orange") return 0xFFA500FFU;
    if (value.starts_with("rgb(")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        if (std::sscanf(value.c_str(), "rgb(%d, %d, %d)", &red, &green, &blue) == 3
            || std::sscanf(value.c_str(), "rgb(%d,%d,%d)", &red, &green, &blue) == 3) {
            return static_cast<uint32_t>(std::clamp(red, 0, 255)) << 24U
                | static_cast<uint32_t>(std::clamp(green, 0, 255)) << 16U
                | static_cast<uint32_t>(std::clamp(blue, 0, 255)) << 8U
                | 0xFFU;
        }
    }
    if (value.starts_with("rgba(")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        float alpha = 0;
        if (std::sscanf(value.c_str(), "rgba(%d, %d, %d, %f)", &red, &green, &blue, &alpha) == 4
            || std::sscanf(value.c_str(), "rgba(%d,%d,%d,%f)", &red, &green, &blue, &alpha) == 4) {
            return static_cast<uint32_t>(std::clamp(red, 0, 255)) << 24U
                | static_cast<uint32_t>(std::clamp(green, 0, 255)) << 16U
                | static_cast<uint32_t>(std::clamp(blue, 0, 255)) << 8U
                | static_cast<uint32_t>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F);
        }
    }
    if ((value.size() == 4 || value.size() == 5) && value.front() == '#') {
        const auto expand = [](char digit) {
            return parse_hex_pair(digit, digit);
        };
        return static_cast<uint32_t>(expand(value[1])) << 24U
            | static_cast<uint32_t>(expand(value[2])) << 16U
            | static_cast<uint32_t>(expand(value[3])) << 8U
            | (value.size() == 5 ? expand(value[4]) : 0xFFU);
    }
    if (value.size() == 7 && value.front() == '#') {
        return static_cast<uint32_t>(parse_hex_pair(value[1], value[2])) << 24U
            | static_cast<uint32_t>(parse_hex_pair(value[3], value[4])) << 16U
            | static_cast<uint32_t>(parse_hex_pair(value[5], value[6])) << 8U
            | 0xFFU;
    }
    if (value.size() == 9 && value.front() == '#') {
        return static_cast<uint32_t>(parse_hex_pair(value[1], value[2])) << 24U
            | static_cast<uint32_t>(parse_hex_pair(value[3], value[4])) << 16U
            | static_cast<uint32_t>(parse_hex_pair(value[5], value[6])) << 8U
            | parse_hex_pair(value[7], value[8]);
    }
    return 0;
}

float native_document::resolve_length(
    const dom_node& context,
    css_length value,
    float available,
    float fallback) const
{
    if (value.unit == length_unit::em) {
        return value.value * resolved_font_size(context);
    }
    if (value.unit == length_unit::rem) {
        auto* root = &context;
        while (root->parent != nullptr) root = root->parent;
        return value.value * resolved_font_size(*root);
    }
    return resolve_length(value, available, fallback);
}

float native_document::resolve_length(css_length value, float available, float fallback) const
{
    switch (value.unit) {
    case length_unit::pixels:
        return value.value;
    case length_unit::percent:
        return available * value.value / 100.0F + value.pixel_offset;
    case length_unit::em:
    case length_unit::rem:
        // Callers resolving element-authored CSS lengths must use the
        // context-aware overload. Preserve the root default here only for
        // non-element fallback paths.
        return value.value * 14.0F;
    case length_unit::viewport_width:
        return viewport_width_ * value.value / 100.0F;
    case length_unit::viewport_height:
        return viewport_height_ * value.value / 100.0F;
    case length_unit::max_content:
    case length_unit::min_content:
    case length_unit::fit_content:
        return fallback;
    case length_unit::automatic:
    default:
        return fallback;
    }
}

bool native_document::is_specified(css_length value)
{
    return value.unit != length_unit::automatic;
}

bool native_document::matches_selector(const dom_node& node, const std::string& selector)
{
    if (selector.empty()) return false;
    const auto selector_end = selector.find_first_of(" [:");
    const auto simple = selector.substr(0, selector_end);
    if (simple.empty() || simple == "*") return true;
    if (simple.front() == '#') return node.id_attribute == simple.substr(1);
    if (simple.front() == '.') {
        const auto wanted = simple.substr(1);
        size_t start = 0;
        while (start < node.class_name.size()) {
            const auto end = node.class_name.find(' ', start);
            const auto count = end == std::string::npos ? std::string::npos : end - start;
            if (node.class_name.substr(start, count) == wanted) return true;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return false;
    }
    return node.tag == simple;
}

void native_document::collect_matches(
    dom_node& node,
    const std::string& selector,
    std::vector<dom_node*>& result)
{
    if (matches_selector(node, selector)) result.push_back(&node);
    for (auto* child : node.children) {
        collect_matches(*child, selector, result);
    }
}
