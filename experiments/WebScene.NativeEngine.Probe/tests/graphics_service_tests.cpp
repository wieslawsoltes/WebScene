#include "graphics/graphics_service.h"
#include "graphics/engine_wake.h"
#include <GLES2/gl2.h>
#include <iostream>
#include <cstring>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; } require(rejected); }
int executed_commands=0;
void set_color(graphics_service& service,std::span<const std::byte> upload,
               const graphics_command::arguments& values) noexcept {
    resource_handle<angle_context> handle{values[0],values[1],static_cast<uint32_t>(values[2])};
    float color[4]{};
    if (upload.size()!=sizeof(color)) std::terminate();
    std::memcpy(color,upload.data(),sizeof(color));
    service.with_angle_context(handle,[&] { glClearColor(color[0],color[1],color[2],color[3]); });
    ++executed_commands;
}
// Commands translate expected context loss without dropping later FIFO work.
std::array<int,4> loss_order{};
size_t loss_count=0;
void context_loss_command(graphics_service& service,std::span<const std::byte>,
                          const graphics_command::arguments& values) noexcept {
    resource_handle<angle_context> handle{values[0],values[1],static_cast<uint32_t>(values[2])};
    bool entered=false;
    try {
        service.with_angle_context(handle,[&] {
            entered=true;
            if(values[3]==1) {
                using request_proc=void (GL_APIENTRY *)(const GLchar*);
                using lose_proc=void (GL_APIENTRY *)(GLenum,GLenum);
                auto request=reinterpret_cast<request_proc>(eglGetProcAddress("glRequestExtensionANGLE"));
                auto lose=reinterpret_cast<lose_proc>(eglGetProcAddress("glLoseContextCHROMIUM"));
                require(request && lose);
                request("GL_CHROMIUM_lose_context");
                require(glGetError()==GL_NO_ERROR);
                lose(GL_GUILTY_CONTEXT_RESET_EXT,GL_INNOCENT_CONTEXT_RESET_EXT);
            } else {
                glClearColor(0.25f,0.5f,0.75f,1);
                GLfloat color[4]{};glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
                require(glGetError()==GL_NO_ERROR && color[1]==0.5f);
            }
        });
        loss_order.at(loss_count++)=3;
    } catch(const angle_context_lost&) {
        loss_order.at(loss_count++)=entered ? 1 : 2;
    }
}
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
    auto endpoint=b.command_endpoint(2,sizeof(float)*4);
    graphics_command command{set_color,{second.table,second.generation,second.slot}};
    std::array<float,4> upload{1,0,0,1};
    std::thread producer([&] {
        require(endpoint->enqueue(command,std::as_bytes(std::span(upload)))==enqueue_result::accepted);
        upload={0,0,1,1};
        require(endpoint->enqueue(command,std::as_bytes(std::span(upload)))==enqueue_result::accepted);
        require(endpoint->enqueue(command,std::as_bytes(std::span(upload)))==enqueue_result::full);
        upload={0,0,0,0};
    });
    producer.join();
    require(b.has_ready_work() && executed_commands==0);
    require(b.drain_commands(1)==1);
    b.with_angle_context(second,[] { GLfloat color[4]{}; glGetFloatv(GL_COLOR_CLEAR_VALUE,color); require(color[0]==1 && color[2]==0); });
    require(b.drain_commands(1)==1);
    b.with_angle_context(second,[] { GLfloat color[4]{}; glGetFloatv(GL_COLOR_CLEAR_VALUE,color); require(color[0]==0 && color[2]==1); });
    auto queue_counters=b.metrics().commands;
    require(queue_counters.depth==0 && queue_counters.high_water==2 && queue_counters.upload_bytes==32);
    require(executed_commands==2);
    b.dawn(); require(b.dawn_initialized() && !b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds(100));
    auto mailbox=b.dawn().completions();
    resource_owner owner{b.engine_identity(),new_owner_token(),0};
    auto cancelled_ticket=mailbox->reserve(2,owner).value();
    mailbox->cancel_owner(owner);
    require(b.pump([](auto record) { require(record.status==completion_status::cancelled); })==1);
    require(!mailbox->has_ready() && mailbox->has_pending());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))<=std::chrono::milliseconds(1));
    require(!mailbox->publish(cancelled_ticket,completion_status::success));
    require(!b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds(100));
    auto ticket=mailbox->reserve(1,owner).value();
    require(mailbox->has_pending());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))<=std::chrono::milliseconds(1));
    b.pump([](auto) {});
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))<=std::chrono::milliseconds(1));
    require(endpoint->enqueue(command,std::as_bytes(std::span(upload)))==enqueue_result::accepted);
    b.close(); require(b.live_contexts()==0);
    require(executed_commands==3 && endpoint->metrics().depth==0);
    require(endpoint->enqueue(command)==enqueue_result::closed);
    require(b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds::zero());
    require(!mailbox->publish(ticket,completion_status::success));
    require(b.pump([](auto record) { require(record.status==completion_status::cancelled); })==1);
    require(!b.has_ready_work());
    require(b.recommended_idle_wait(std::chrono::milliseconds(100))==std::chrono::milliseconds(100));
    graphics_service gc(wake);
    auto gc_context=gc.create_angle_context(backend,2);
    auto gc_commands=gc.command_endpoint(2,sizeof(float)*4);
    auto gc_releases=gc.release_endpoint(1);
    auto release_record=graphics_service::deferred_context_release(gc_context);
    auto registration=gc_releases->reserve(release_record).value();
    require(!gc_releases->reserve(release_record));
    graphics_command gc_command{set_color,{gc_context.table,gc_context.generation,gc_context.slot}};
    require(gc_commands->enqueue(gc_command,std::as_bytes(std::span(upload)))==enqueue_result::accepted);
    require(gc_commands->enqueue(gc_command,std::as_bytes(std::span(upload)))==enqueue_result::accepted);
    require(gc_commands->enqueue(gc_command)==enqueue_result::full);
    std::thread gc_finalizer([&] { require(gc_releases->publish(registration)); });
    gc_finalizer.join();
    require(!gc_releases->publish(registration));
    require(gc.live_contexts()==1 && gc.metrics().release_registrations==1);
    require(gc.drain_commands(1)==1 && gc.live_contexts()==1);
    require(gc.drain_commands(1)==1 && gc.live_contexts()==0);
    require(gc.metrics().release_registrations==0);
    auto abandoned=gc_releases->reserve(release_record).value();
    require(abandoned.generation!=registration.generation);
    require(!gc_releases->publish(registration));
    gc.close();
    require(!gc_releases->publish(abandoned) && gc_releases->occupied()==0);
    // A delayed finalizer may refer to a slot reused by a new native context.
    // Repeat real context teardown/recreation while forcing slot reuse.
    graphics_service recycled(wake,1);
    recycled.command_endpoint(1,0);
    auto recycled_releases=recycled.release_endpoint(1);
    for (int iteration=0;iteration<64;++iteration) {
        auto old=recycled.create_angle_context(backend,2);
        auto late=recycled_releases->reserve(graphics_service::deferred_context_release(old)).value();
        recycled.destroy_angle_context(old);
        auto replacement=recycled.create_angle_context(backend,2);
        require(replacement.slot==old.slot && replacement.generation!=old.generation);
        require(recycled_releases->publish(late));
        recycled.pump([](auto) { throw std::runtime_error("unexpected context completion"); });
        require(recycled.live_contexts()==1);
        recycled.with_angle_context(replacement,[] {
            glClearColor(0.25f,0.5f,0.75f,1);
            GLfloat color[4]{};
            glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
            require(color[0]==0.25f && color[1]==0.5f && color[2]==0.75f);
        });
        recycled.destroy_angle_context(replacement);
        auto baseline=recycled.metrics();
        require(baseline.live_contexts==0 && baseline.release_registrations==0 && baseline.commands.depth==0);
    }
    recycled.close();
    // Loss in one queued command is reported immediately, subsequent commands
    // for that context reject before execution, and independent work continues.
    graphics_service loss_service(wake);
    auto lost_context=loss_service.create_angle_context(backend,2);
    auto surviving_context=loss_service.create_angle_context(backend,2);
    auto loss_queue=loss_service.command_endpoint(4,0);
    const auto enqueue_loss=[&](auto handle,uint64_t inject) {
        require(loss_queue->enqueue({context_loss_command,
            {handle.table,handle.generation,handle.slot,inject}})==enqueue_result::accepted);
    };
    enqueue_loss(lost_context,1);
    enqueue_loss(lost_context,0);
    enqueue_loss(surviving_context,0);
    enqueue_loss(lost_context,0);
    require(loss_service.drain_commands(4)==4);
    require(loss_count==4 && loss_order==std::array<int,4>{1,2,3,2});
    require(eglGetCurrentContext()==EGL_NO_CONTEXT);
    loss_service.destroy_angle_context(lost_context);
    loss_service.destroy_angle_context(surviving_context);
    require(loss_service.live_contexts()==0 && loss_queue->metrics().depth==0);
    loss_service.close();
    graphics_service delivery(wake);
    auto delivery_mailbox=delivery.dawn().completions();
    auto delivery_ticket=delivery_mailbox->reserve(1,{delivery.engine_identity(),new_owner_token(),0}).value();
    require(delivery_mailbox->publish(delivery_ticket,completion_status::success));
    rejects([&] {
        delivery.pump([&](auto) {
            rejects([&] { delivery.close(); });
            rejects([&] { delivery.pump([](auto) {}); });
            throw std::runtime_error("delivery exception");
        });
    });
    delivery.close(); // Exception unwinding must release the active pump guard.
    auto disposed=std::make_unique<graphics_service>(wake);
    auto late_endpoint=disposed->command_endpoint(1,0);
    disposed.reset();
    require(late_endpoint->enqueue(command)==enqueue_result::closed);
    std::cout << "lazy graphics service and cross-engine ANGLE lifetime isolation passed\n";
}
