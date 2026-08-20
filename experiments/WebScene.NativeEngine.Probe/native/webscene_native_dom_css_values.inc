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
                    : calculated->unit == calc_unit::viewport_width
                        ? calculated->bound == calc_value::bound_kind::minimum
                            ? length_unit::viewport_width_capped
                            : calculated->bound == calc_value::bound_kind::maximum
                                ? length_unit::viewport_width_floored
                                : length_unit::viewport_width
                        : calculated->unit == calc_unit::viewport_height
                            ? calculated->bound == calc_value::bound_kind::minimum
                                ? length_unit::viewport_height_capped
                                : calculated->bound == calc_value::bound_kind::maximum
                                    ? length_unit::viewport_height_floored
                                    : length_unit::viewport_height
                            : length_unit::pixels,
                static_cast<float>(calculated->bound == calc_value::bound_kind::none
                    ? calculated->pixel_offset
                    : calculated->pixel_bound)};
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
        // Transform operands may themselves be functions. In particular,
        // translate(calc(...), var(...)) must close and split at the outer
        // transform level instead of treating calc's first ')' as the end.
        auto close = std::string::npos;
        auto depth = 1;
        for (auto index = start; index < value.size(); ++index) {
            if (value[index] == '(') {
                ++depth;
            } else if (value[index] == ')' && --depth == 0) {
                close = index;
                break;
            }
        }
        if (close == std::string::npos) return std::vector<std::string>{};
        const auto arguments = std::string_view(value).substr(start, close - start);
        std::vector<std::string> result;
        auto token_start = std::string_view::npos;
        depth = 0;
        for (size_t index = 0; index <= arguments.size(); ++index) {
            const auto at_end = index == arguments.size();
            const auto character = at_end ? '\0' : arguments[index];
            const auto separator = at_end
                || (depth == 0
                    && (character == ','
                        || std::isspace(static_cast<unsigned char>(character))));
            if (!separator && token_start == std::string_view::npos) token_start = index;
            if (!at_end && character == '(') ++depth;
            else if (!at_end && character == ')' && depth > 0) --depth;
            if (separator && token_start != std::string_view::npos) {
                result.emplace_back(arguments.substr(token_start, index - token_start));
                token_start = std::string_view::npos;
            }
        }
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
    auto normalized = value;
    const auto first = normalized.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return 0;
    const auto last = normalized.find_last_not_of(" \t\r\n");
    normalized = normalized.substr(first, last - first + 1U);
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });

    if (normalized == "transparent") return 0;
    if (normalized == "black") return 0x000000FFU;
    if (normalized == "white") return 0xFFFFFFFFU;
    if (normalized == "red") return 0xFF0000FFU;
    if (normalized == "green") return 0x008000FFU;
    if (normalized == "lime") return 0x00FF00FFU;
    if (normalized == "blue") return 0x0000FFFFU;
    if (normalized == "yellow") return 0xFFFF00FFU;
    if (normalized == "cyan" || normalized == "aqua") return 0x00FFFFFFU;
    if (normalized == "magenta" || normalized == "fuchsia") return 0xFF00FFFFU;
    if (normalized == "gray" || normalized == "grey") return 0x808080FFU;
    if (normalized == "silver") return 0xC0C0C0FFU;
    if (normalized == "maroon") return 0x800000FFU;
    if (normalized == "purple") return 0x800080FFU;
    if (normalized == "olive") return 0x808000FFU;
    if (normalized == "navy") return 0x000080FFU;
    if (normalized == "teal") return 0x008080FFU;
    if (normalized == "orange") return 0xFFA500FFU;
    if (normalized.starts_with("color(srgb ") && normalized.ends_with(')')) {
        auto components = normalized.substr(11U, normalized.size() - 12U);
        std::replace(components.begin(), components.end(), '/', ' ');
        std::istringstream stream(components);
        float red = 0;
        float green = 0;
        float blue = 0;
        float alpha = 1;
        if (stream >> red >> green >> blue) {
            static_cast<void>(stream >> alpha);
            const auto channel = [](float component) {
                return static_cast<uint32_t>(std::lround(
                    std::clamp(component, 0.0F, 1.0F) * 255.0F));
            };
            return channel(red) << 24U
                | channel(green) << 16U
                | channel(blue) << 8U
                | channel(alpha);
        }
    }
    if (normalized.starts_with("color-mix(") && normalized.ends_with(')')) {
        auto arguments = std::string_view(normalized).substr(
            10U, normalized.size() - 11U);
        if (arguments.starts_with("in srgb,")) {
            arguments.remove_prefix(8U);
            while (!arguments.empty()
                && std::isspace(static_cast<unsigned char>(arguments.front()))) {
                arguments.remove_prefix(1U);
            }
            size_t separator = std::string_view::npos;
            auto depth = 0;
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (arguments[index] == '(') ++depth;
                else if (arguments[index] == ')') --depth;
                else if (arguments[index] == ',' && depth == 0) {
                    separator = index;
                    break;
                }
            }
            if (separator != std::string_view::npos) {
                auto parse_stop = [](std::string_view component) {
                    while (!component.empty()
                        && std::isspace(static_cast<unsigned char>(component.front()))) {
                        component.remove_prefix(1U);
                    }
                    while (!component.empty()
                        && std::isspace(static_cast<unsigned char>(component.back()))) {
                        component.remove_suffix(1U);
                    }
                    auto weight = -1.0F;
                    if (component.ends_with('%')) {
                        const auto space = component.find_last_of(" \t\r\n");
                        if (space != std::string_view::npos) {
                            weight = parse_number(component.substr(
                                space + 1U, component.size() - space - 2U), -1.0F);
                            component = component.substr(0, space);
                            while (!component.empty()
                                && std::isspace(static_cast<unsigned char>(component.back()))) {
                                component.remove_suffix(1U);
                            }
                        }
                    }
                    return std::pair{std::string(component), weight};
                };
                auto [first_color, first_weight] = parse_stop(
                    arguments.substr(0, separator));
                auto [second_color, second_weight] = parse_stop(
                    arguments.substr(separator + 1U));
                if (first_weight < 0 && second_weight < 0) {
                    first_weight = second_weight = 50.0F;
                } else if (first_weight < 0) {
                    first_weight = 100.0F - second_weight;
                } else if (second_weight < 0) {
                    second_weight = 100.0F - first_weight;
                }
                const auto total = first_weight + second_weight;
                if (first_weight >= 0 && second_weight >= 0 && total > 0) {
                    const auto first_rgba = parse_color(first_color);
                    const auto second_rgba = parse_color(second_color);
                    const auto weight1 = first_weight / total;
                    const auto weight2 = second_weight / total;
                    const auto alpha1 = static_cast<float>(first_rgba & 0xFFU) / 255.0F;
                    const auto alpha2 = static_cast<float>(second_rgba & 0xFFU) / 255.0F;
                    const auto alpha = alpha1 * weight1 + alpha2 * weight2;
                    const auto mixed_channel = [&](uint32_t shift) {
                        if (alpha <= 0.0F) return uint32_t{0U};
                        const auto channel1 = static_cast<float>((first_rgba >> shift) & 0xFFU);
                        const auto channel2 = static_cast<float>((second_rgba >> shift) & 0xFFU);
                        return static_cast<uint32_t>(std::lround(std::clamp(
                            (channel1 * alpha1 * weight1 + channel2 * alpha2 * weight2)
                                / alpha,
                            0.0F,
                            255.0F)));
                    };
                    return mixed_channel(24U) << 24U
                        | mixed_channel(16U) << 16U
                        | mixed_channel(8U) << 8U
                        | static_cast<uint32_t>(std::lround(
                            std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
                }
            }
        }
    }
    if (normalized.starts_with("rgb(")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        if (std::sscanf(normalized.c_str(), "rgb(%d, %d, %d)", &red, &green, &blue) == 3
            || std::sscanf(normalized.c_str(), "rgb(%d,%d,%d)", &red, &green, &blue) == 3) {
            return static_cast<uint32_t>(std::clamp(red, 0, 255)) << 24U
                | static_cast<uint32_t>(std::clamp(green, 0, 255)) << 16U
                | static_cast<uint32_t>(std::clamp(blue, 0, 255)) << 8U
                | 0xFFU;
        }
    }
    if (normalized.starts_with("rgba(")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        float alpha = 0;
        if (std::sscanf(normalized.c_str(), "rgba(%d, %d, %d, %f)", &red, &green, &blue, &alpha) == 4
            || std::sscanf(normalized.c_str(), "rgba(%d,%d,%d,%f)", &red, &green, &blue, &alpha) == 4) {
            return static_cast<uint32_t>(std::clamp(red, 0, 255)) << 24U
                | static_cast<uint32_t>(std::clamp(green, 0, 255)) << 16U
                | static_cast<uint32_t>(std::clamp(blue, 0, 255)) << 8U
                | static_cast<uint32_t>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F);
        }
    }
    if ((normalized.size() == 4 || normalized.size() == 5) && normalized.front() == '#') {
        const auto expand = [](char digit) {
            return parse_hex_pair(digit, digit);
        };
        return static_cast<uint32_t>(expand(normalized[1])) << 24U
            | static_cast<uint32_t>(expand(normalized[2])) << 16U
            | static_cast<uint32_t>(expand(normalized[3])) << 8U
            | (normalized.size() == 5 ? expand(normalized[4]) : 0xFFU);
    }
    if (normalized.size() == 7 && normalized.front() == '#') {
        return static_cast<uint32_t>(parse_hex_pair(normalized[1], normalized[2])) << 24U
            | static_cast<uint32_t>(parse_hex_pair(normalized[3], normalized[4])) << 16U
            | static_cast<uint32_t>(parse_hex_pair(normalized[5], normalized[6])) << 8U
            | 0xFFU;
    }
    if (normalized.size() == 9 && normalized.front() == '#') {
        return static_cast<uint32_t>(parse_hex_pair(normalized[1], normalized[2])) << 24U
            | static_cast<uint32_t>(parse_hex_pair(normalized[3], normalized[4])) << 16U
            | static_cast<uint32_t>(parse_hex_pair(normalized[5], normalized[6])) << 8U
            | parse_hex_pair(normalized[7], normalized[8]);
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
        return viewport_width_ * value.value / 100.0F + value.pixel_offset;
    case length_unit::viewport_height:
        return viewport_height_ * value.value / 100.0F + value.pixel_offset;
    case length_unit::viewport_width_capped:
        return std::min(viewport_width_ * value.value / 100.0F, value.pixel_offset);
    case length_unit::viewport_height_capped:
        return std::min(viewport_height_ * value.value / 100.0F, value.pixel_offset);
    case length_unit::viewport_width_floored:
        return std::max(viewport_width_ * value.value / 100.0F, value.pixel_offset);
    case length_unit::viewport_height_floored:
        return std::max(viewport_height_ * value.value / 100.0F, value.pixel_offset);
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
