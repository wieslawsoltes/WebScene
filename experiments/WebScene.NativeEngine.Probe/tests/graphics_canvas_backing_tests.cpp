#include "graphics/canvas_backing.h"
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("canvas backing requirement failed"); }
int main() {
    canvas_backing a,b;
    require(a.identity()!=b.identity());
    require(a.width()==300 && a.height()==150 && a.mode()==canvas_context_mode::none);
    require(!a.claim_context(canvas_context_mode::none));
    require(a.claim_context(canvas_context_mode::webgpu));
    require(a.claim_context(canvas_context_mode::webgpu));
    require(!a.claim_context(canvas_context_mode::two_d) && !a.claim_context(canvas_context_mode::webgl2));
    auto identity=a.identity(),generation=a.allocation_generation();
    for (int i=0;i<1000;++i) a.publish_content();
    require(a.content_serial()==1000 && a.allocation_generation()==generation && a.identity()==identity);
    a.reset_bitmap(300,150);
    require(a.content_serial()==1001 && a.allocation_generation()==generation);
    a.reset_bitmap(640,480);
    require(a.width()==640 && a.height()==480 && a.allocation_generation()==generation+1);
    require(a.identity()==identity && a.mode()==canvas_context_mode::webgpu);
    a.reset_bitmap(0,0);
    require(a.width()==0 && a.height()==0 && !a.claim_context(canvas_context_mode::two_d));
    require(b.claim_context(canvas_context_mode::two_d));
    std::cout << "canvas identity, exclusive context and independent content/allocation versions passed\n";
}
