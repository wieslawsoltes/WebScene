#pragma once
namespace webscene::graphics {
// Host-selected transport; never accepted through a JavaScript descriptor.
enum class webgpu_canvas_interop { none,iosurface };
}
