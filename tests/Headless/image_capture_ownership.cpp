#include <graphics/native_image_capture.h>
#include <memory>
#include <stdexcept>
using namespace webscene::graphics;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct provider final:native_image_capture_provider {
    bool fail=false;
    captured_native_image capture(std::shared_ptr<owned_image_pool::consumer> image,image_capture_options)override {
        if(fail)throw std::runtime_error("deliberate provider failure");
        return {image->describe(),4,{0,0,255,255}};
    }
};
static auto publish(owned_image_pool& pool){
    auto writer=pool.acquire();check(bool(writer),"Producer reservation failed");
    writer->set_metadata({1,2,3,4,1,4,1,1});writer->begin();auto retained=writer->publish();writer->complete();
    check(bool(retained),"Image publication failed");return std::make_shared<webscene_gpu_image_lease_v3>(std::move(*retained));
}
int main(){
    auto native=std::make_shared<provider>();std::weak_ptr<provider> weak=native;
    std::shared_ptr<webscene_gpu_image_lease_v3> retained;
    {
        owned_image_pool pool(native);retained=publish(pool);
        check(capture_native_image(*retained).pixels.size()==4,"Native provider capture failed");
        check(pool.inspect_occupancy().consumer_pending==0,"Successful synchronous capture leaked a consumer");
        native->fail=true;bool rejected=false;
        try{capture_native_image(*retained);}catch(const std::runtime_error&){rejected=true;}
        check(rejected&&pool.inspect_occupancy().consumer_pending==0,"Throwing provider leaked a consumer");
        native->fail=false;rejected=false;
        try{capture_native_image(*retained,{0,4096});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&pool.inspect_occupancy().consumer_pending==0,"Invalid options acquired a consumer");
        pool.close();
    }
    native.reset();check(!weak.expired(),"Retained image lost its provider at pool close");
    check(capture_native_image(*retained).metadata.content_serial==4,"Closed-pool retained image changed identity");
    retained.reset();check(weak.expired(),"Capture ownership retained a closed provider");
    struct unsupported final:image_provider_lifetime{};
    owned_image_pool other(std::make_shared<unsupported>());auto image=publish(other);bool rejected=false;
    try{capture_native_image(*image);}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&other.inspect_occupancy().consumer_pending==0,"Unsupported provider leaked or pretended to capture");
}
