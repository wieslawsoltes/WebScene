#pragma once
#include <foco/webscene.hpp>
#include "image_lease_abi.h"
namespace webscene::foco_host {
std::shared_ptr<const foco::composition_resource_attachment> make_gpu_image(
    uint32_t node, uint64_t generation,
    std::shared_ptr<const webscene_gpu_image_lease_v3> image);
}
