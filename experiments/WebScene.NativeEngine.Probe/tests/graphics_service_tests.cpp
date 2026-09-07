#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include <GLES2/gl2.h>
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; } require(rejected); }
int main() {
    auto wake=std::make_shared<engine_wake>();
    graphics_service a(wake),b(wake);
    require(!a.dawn_initialized() && !b.dawn_initialized());
    require(!a.has_ready_work());
    require(a.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds(100));
    require(a.pump([](auto) {})==0 && !a.dawn_initialized());
#if defined(__APPLE__)
    constexpr auto backend=EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE;
#elif defined(_WIN32)
    constexpr auto backend=EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
#else
    constexpr auto backend=EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
#endif
    auto first=a.create_angle_context(backend,2),second=b.create_angle_context(backend,2);
    rejects([&] { b.with_angle_context(first,[] {}); });
    a.with_angle_context(first,[] { glClearColor(1,0,0,1); });
    b.with_angle_context(second,[] { glClearColor(0,1,0,1); });
    a.with_angle_context(first,[&] {
        rejects([&] { a.destroy_angle_context(first); });
        rejects([&] { a.close(); });
        require(a.live_contexts()==1);
    });
    rejects([&] { a.with_angle_context(first,[] { throw std::runtime_error("execution failed"); }); });
    a.destroy_angle_context(first);
    rejects([&] { a.with_angle_context(first,[] {}); });
    a.close();
    b.with_angle_context(second,[] { GLfloat color[4]{}; glGetFloatv(GL_COLOR_CLEAR_VALUE,color); require(color[1]==1); });
    require(a.live_contexts()==0 && b.live_contexts()==1);
    rejects([&] { a.dawn(); });
    b.dawn(); require(b.dawn_initialized() && !b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds(100));
    auto mailbox=b.dawn().completions();
    resource_owner owner{b.engine_identity(),new_owner_token(),0};
    auto ticket=mailbox->reserve(1,owner).value();
    require(b.has_ready_work());
    b.pump([](auto) {});
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))<=std::chrono::milliseconds(1));
    b.close(); require(b.live_contexts()==0);
    require(b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds::zero());
    require(!mailbox->publish(ticket,completion_status::success));
    require(b.pump([](auto record) { require(record.status==completion_status::cancelled); })==1);
    require(!b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds(100));
    std::cout << "lazy graphics service and cross-engine ANGLE lifetime isolation passed\n";
}
