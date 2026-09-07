#pragma once
#include <cstdint>

namespace webscene::graphics {
enum class image_format : uint32_t { rgba8_unorm=1,bgra8_unorm,rgba16_float,rgba8_srgb,bgra8_srgb };
enum class image_alpha : uint32_t { opaque=1,premultiplied,straight };
enum class image_color_space : uint32_t { srgb=1,display_p3 };
enum class image_orientation : uint32_t { top_left=1,bottom_left };
// Portable values only. Allocation and readiness identities resolve through the
// native provider, never by casting an integer to a texture/Skia pointer.
struct image_metadata {
    uint64_t canvas{},allocation{},allocation_generation{},content_serial{};
    uint64_t producer_timeline{},producer_value{};
    uint32_t width{},height{};
    image_format format{image_format::rgba8_unorm};
    image_alpha alpha{image_alpha::premultiplied};
    image_color_space color_space{image_color_space::srgb};
    image_orientation orientation{image_orientation::top_left};
};
} // namespace webscene::graphics
