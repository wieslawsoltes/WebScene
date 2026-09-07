#include "graphics/dxgi_bridge_contract.h"
#include <stdexcept>
using namespace webscene::graphics;
int main() {
    auto require=[](bool value) { if (!value) throw std::runtime_error("DXGI negotiation requirement failed"); };
    dxgi_endpoint producer{{10,-1,true},dxgi_api::d3d12,0x1f,0x7,true,false};
    auto consumer=producer; consumer.api=dxgi_api::d3d11;
    image_metadata image{1,2,1,1,3,1,64,64};
    auto choice=choose_dxgi_bridge(producer,consumer,image);
    require(choice.status==dxgi_bridge_status::supported && choice.synchronization==dxgi_sync::shared_fence);
    consumer.adapter.low=11; require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::cross_adapter);
    consumer.adapter=producer.adapter; consumer.adapter.valid=false;
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::unknown_adapter);
    consumer=producer; consumer.color_formats=0;
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::unsupported_format);
    consumer=producer; consumer.alpha_modes=0;
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::unsupported_alpha);
    consumer=producer; require(choose_dxgi_bridge(producer,consumer,image,4).status==dxgi_bridge_status::needs_resolve);
    consumer.shared_fence=false; consumer.keyed_mutex=true; producer.keyed_mutex=true;
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::unsupported_synchronization);
    producer.api=consumer.api=dxgi_api::d3d11;
    require(choose_dxgi_bridge(producer,consumer,image).synchronization==dxgi_sync::keyed_mutex);
    consumer.api=static_cast<dxgi_api>(99);
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::unsupported_api);
    consumer.api=dxgi_api::d3d11;
    image.format=static_cast<image_format>(UINT32_MAX);
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::unsupported_format);
    image.format=image_format::rgba8_unorm; image.width=0;
    require(choose_dxgi_bridge(producer,consumer,image).status==dxgi_bridge_status::invalid_dimensions);
}
