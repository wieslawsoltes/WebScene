uint64_t mix_hash(uint64_t hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

uint64_t canvas_layer_content_hash(
    const webscene_canvas_layer& layer,
    const std::vector<webscene_canvas_command>& commands,
    const std::vector<webscene_scene_string>& strings,
    const std::vector<char>& string_bytes)
{
    auto hash = 1469598103934665603ULL;
    const auto command_end =
        static_cast<size_t>(layer.command_offset) + layer.command_count;
    for (auto index = static_cast<size_t>(layer.command_offset);
         index < command_end;
         ++index) {
        const auto& command = commands[index];
        hash = mix_hash(
            hash,
            static_cast<uint64_t>(command.kind) << 32U | command.flags);
        hash = mix_hash(
            hash,
            static_cast<uint64_t>(command.resource_id) << 32U
                | command.reserved);
        for (const auto value : command.data.values) {
            hash = mix_hash(hash, std::bit_cast<uint64_t>(value));
        }
    }

    const auto string_end =
        static_cast<size_t>(layer.string_offset) + layer.string_count;
    for (auto index = static_cast<size_t>(layer.string_offset);
         index < string_end;
         ++index) {
        const auto& value = strings[index];
        hash = mix_hash(hash, value.byte_length);
        const auto byte_end =
            static_cast<size_t>(value.byte_offset) + value.byte_length;
        for (auto byte_index = static_cast<size_t>(value.byte_offset);
             byte_index < byte_end;
             ++byte_index) {
            hash = mix_hash(
                hash,
                static_cast<uint8_t>(string_bytes[byte_index]));
        }
    }
    return hash;
}

void store_maximum(std::atomic<uint64_t>& target, uint64_t value)
{
    auto current = target.load(std::memory_order_relaxed);
    while (current < value
        && !target.compare_exchange_weak(
            current,
            value,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}

bool command_uses_dom_string(const webscene_scene_command& command)
{
    return command.kind >= 3U && command.kind <= 6U;
}

std::string_view command_dom_string(
    const scene& owner,
    const webscene_scene_command& command)
{
    if (!command_uses_dom_string(command)
        || command.flags >= owner.canvas_strings.size()) {
        return {};
    }
    const auto& value = owner.canvas_strings[command.flags];
    if (value.byte_offset > owner.canvas_string_bytes.size()
        || value.byte_length > owner.canvas_string_bytes.size() - value.byte_offset) {
        return {};
    }
    return std::string_view(
        owner.canvas_string_bytes.data() + value.byte_offset,
        value.byte_length);
}

bool same_dom_command_visual(
    const scene& previous_owner,
    const webscene_scene_command& previous,
    const scene& next_owner,
    const webscene_scene_command& next)
{
    if (previous.kind != next.kind
        || previous.node_id != next.node_id
        || previous.x != next.x
        || previous.y != next.y
        || previous.width != next.width
        || previous.height != next.height
        || previous.rgba != next.rgba
        || previous.radius_top_left != next.radius_top_left
        || previous.radius_top_right != next.radius_top_right
        || previous.radius_bottom_right != next.radius_bottom_right
        || previous.radius_bottom_left != next.radius_bottom_left
        || previous.stroke_width != next.stroke_width) {
        return false;
    }
    if (command_uses_dom_string(next)) {
        return command_dom_string(previous_owner, previous)
            == command_dom_string(next_owner, next);
    }
    return previous.flags == next.flags;
}

webscene_damage_rect command_damage_bounds(const webscene_scene_command& command)
{
    auto left = command.x;
    auto top = command.y;
    auto right = command.x + command.width;
    auto bottom = command.y + command.height;
    if (command.kind == 2U) {
        left = std::min(command.x, command.width);
        top = std::min(command.y, command.height);
        right = std::max(command.x, command.width);
        bottom = std::max(command.y, command.height);
    } else if (command.kind >= 4U && command.kind <= 6U
        && std::abs(command.stroke_width) > 0.001F) {
        // A rotated SVG can extend beyond its untransformed rectangular box.
        // Use the circumscribed square so both the old and new pixels are
        // invalidated throughout the transition.
        const auto center_x = command.x + command.width * 0.5F;
        const auto center_y = command.y + command.height * 0.5F;
        const auto radius = std::sqrt(
            command.width * command.width + command.height * command.height) * 0.5F;
        left = center_x - radius;
        top = center_y - radius;
        right = center_x + radius;
        bottom = center_y + radius;
    }
    constexpr auto antialias_padding = 2.0F;
    return {
        left - antialias_padding,
        top - antialias_padding,
        std::max(0.0F, right - left + antialias_padding * 2.0F),
        std::max(0.0F, bottom - top + antialias_padding * 2.0F)};
}

bool append_localized_dom_damage(
    const scene& previous,
    const scene& next,
    float viewport_width,
    float viewport_height,
    std::vector<webscene_damage_rect>& damage)
{
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto trace = std::getenv("WEBSCENE_PROBE_PROFILE_ANIMATION") != nullptr;
#endif
    if (previous.commands.size() != next.commands.size()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) std::fprintf(
            stderr,
            "[Native Engine Probe] DOM damage fallback: command-count %zu -> %zu\n",
            previous.commands.size(),
            next.commands.size());
#endif
        return false;
    }

    auto changed = false;
    auto left = viewport_width;
    auto top = viewport_height;
    auto right = 0.0F;
    auto bottom = 0.0F;
    const auto include = [&](const webscene_scene_command& command) {
        const auto bounds = command_damage_bounds(command);
        left = std::min(left, bounds.x);
        top = std::min(top, bounds.y);
        right = std::max(right, bounds.x + bounds.width);
        bottom = std::max(bottom, bounds.y + bounds.height);
    };
    for (size_t index = 0; index < next.commands.size(); ++index) {
        const auto& old_command = previous.commands[index];
        const auto& new_command = next.commands[index];
        if (old_command.kind != new_command.kind
            || old_command.node_id != new_command.node_id) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            if (trace) std::fprintf(
                stderr,
                "[Native Engine Probe] DOM damage fallback: command identity at %zu "
                "kind %u/%u node %u/%u\n",
                index,
                old_command.kind,
                new_command.kind,
                old_command.node_id,
                new_command.node_id);
#endif
            return false;
        }
        if (same_dom_command_visual(previous, old_command, next, new_command)) continue;
        changed = true;
        include(old_command);
        include(new_command);
    }
    if (!changed) return false;

    left = std::clamp(left, 0.0F, viewport_width);
    top = std::clamp(top, 0.0F, viewport_height);
    right = std::clamp(right, left, viewport_width);
    bottom = std::clamp(bottom, top, viewport_height);
    const auto width = right - left;
    const auto height = bottom - top;
    const auto viewport_area = std::max(1.0F, viewport_width * viewport_height);
    if (width <= 0 || height <= 0 || width * height > viewport_area * 0.4F) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) std::fprintf(
            stderr,
            "[Native Engine Probe] DOM damage fallback: changed bounds %.1f,%.1f %.1fx%.1f "
            "(%.1f%%)\n",
            left,
            top,
            width,
            height,
            width * height * 100.0F / viewport_area);
#endif
        return false;
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    if (trace) std::fprintf(
        stderr,
        "[Native Engine Probe] localized DOM damage: %.1f,%.1f %.1fx%.1f\n",
        left,
        top,
        width,
        height);
#endif
    damage.push_back({left, top, width, height});
    return true;
}
