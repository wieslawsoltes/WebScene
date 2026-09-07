#pragma once
#include "completion_mailbox.h"
#include "resource_table.h"
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
// One logical WebGPU device. Adapter/device references remain native and belong
// to the engine thread; callback captures retain only the completion mailbox.
struct device_loss_signal {
    std::atomic<bool> lost{};
    std::shared_ptr<completion_wake> wake;
    explicit device_loss_signal(std::shared_ptr<completion_wake> value) : wake(std::move(value)) {}
    static void configure(wgpu::DeviceDescriptor& descriptor,std::shared_ptr<device_loss_signal> signal) {
        if (!signal) throw std::invalid_argument("device loss signal is required");
        descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
            [signal](const wgpu::Device&,wgpu::DeviceLostReason reason,wgpu::StringView) {
                if (reason!=wgpu::DeviceLostReason::Destroyed && reason!=wgpu::DeviceLostReason::CallbackCancelled) {
                    signal->lost.store(true,std::memory_order_release);
                    if (signal->wake) signal->wake->signal();
                }
            });
    }
};
class dawn_device {
    const std::thread::id thread_=std::this_thread::get_id();
    const resource_owner owner_;
    std::shared_ptr<completion_mailbox> mailbox_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    bool closed_{};
    bool lost_{};
    std::shared_ptr<device_loss_signal> loss_;
    resource_table<wgpu::Buffer> buffers_;
    size_t active_buffer_scopes_{};
    resource_table<wgpu::ShaderModule> shaders_;
    size_t active_shader_scopes_{};
    resource_table<wgpu::RenderPipeline> render_pipelines_;
    size_t active_render_pipeline_scopes_{};
    resource_table<wgpu::Texture> textures_;
    resource_table<wgpu::TextureView> texture_views_;
    size_t active_texture_scopes_{},active_texture_view_scopes_{};
    resource_table<wgpu::CommandEncoder> command_encoders_;
    resource_table<wgpu::RenderPassEncoder> render_passes_;
    resource_table<wgpu::CommandBuffer> command_buffers_;
    size_t active_command_scopes_{};
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_)
            throw std::logic_error("Dawn device requires its engine thread");
    }
public:
    dawn_device(uint64_t engine,std::shared_ptr<completion_mailbox> mailbox,
                wgpu::Adapter adapter,wgpu::Device device,std::shared_ptr<device_loss_signal> loss={},size_t buffer_capacity=1024,size_t shader_capacity=1024,size_t render_pipeline_capacity=1024,size_t texture_capacity=1024,size_t texture_view_capacity=4096,size_t command_capacity=1024)
        : owner_{engine,new_owner_token(),0},mailbox_(std::move(mailbox)),
          adapter_(std::move(adapter)),device_(std::move(device)),loss_(std::move(loss)),buffers_(buffer_capacity,owner_),shaders_(shader_capacity,owner_),render_pipelines_(render_pipeline_capacity,owner_),textures_(texture_capacity,owner_),texture_views_(texture_view_capacity,owner_),command_encoders_(command_capacity,owner_),render_passes_(command_capacity,owner_),command_buffers_(command_capacity,owner_) {
        if (!engine || !mailbox_ || !adapter_ || !device_)
            throw std::invalid_argument("Dawn device requires native ownership");
    }
    dawn_device(const dawn_device&)=delete;
    dawn_device& operator=(const dawn_device&)=delete;
    ~dawn_device() { if (std::this_thread::get_id()!=thread_) std::terminate(); close(); }
    resource_owner owner() const { check_thread(); return owner_; }
    const wgpu::Adapter& adapter() const { check_thread(); return adapter_; }
    const wgpu::Device& native() const {
        check_thread();
        if (closed_ || lost_ || (loss_ && loss_->lost.load(std::memory_order_acquire)))
            throw std::logic_error("Dawn device is closed or lost");
        return device_;
    }
    // Internal native descriptor entry point. Browser descriptor validation and
    // error-object handling must precede this call in the JavaScript binding.
    resource_handle<wgpu::Buffer> create_buffer(const wgpu::BufferDescriptor& descriptor) {
        const auto& device=native();
        if (!buffers_.can_insert()) throw std::length_error("graphics buffer limit reached");
        auto buffer=device.CreateBuffer(&descriptor);
        if (!buffer) throw std::runtime_error("Dawn did not return a buffer");
        return buffers_.insert(owner_,std::make_unique<wgpu::Buffer>(std::move(buffer)));
    }
    template<class Execute> void with_buffer(resource_handle<wgpu::Buffer> handle,Execute execute) {
        check_thread();
        const auto& buffer=buffers_.get(handle,owner_);
        struct guard {
            size_t& count;
            explicit guard(size_t& value) : count(value) { ++count; }
            ~guard() { --count; }
        } scope(active_buffer_scopes_);
        execute(buffer);
    }
    // WebGPU destroy invalidates the native allocation, but the wrapper remains
    // valid for metadata and repeated destroy calls until it is itself released.
    void destroy_buffer(resource_handle<wgpu::Buffer> handle) {
        check_thread();
        if (active_buffer_scopes_) throw std::logic_error("Cannot destroy buffers during execution");
        buffers_.get(handle,owner_).Destroy();
    }
    // Wrapper collection releases only this reference. Dawn/queued operations
    // retain their own native references; collection must never call Destroy.
    void release_buffer(resource_handle<wgpu::Buffer> handle) {
        check_thread();
        if (active_buffer_scopes_) throw std::logic_error("Cannot release buffers during execution");
        buffers_.destroy(handle,owner_);
    }
    size_t live_buffers() const { check_thread(); return buffers_.resident_count(); }
    resource_handle<wgpu::ShaderModule> create_shader_module(const wgpu::ShaderModuleDescriptor& descriptor) {
        const auto& device=native();
        if(!shaders_.can_insert())throw std::length_error("Graphics shader-module capacity exhausted");
        auto shader=device.CreateShaderModule(&descriptor);
        if(!shader)throw std::runtime_error("Dawn did not return a shader module");
        return shaders_.insert(owner_,std::make_unique<wgpu::ShaderModule>(std::move(shader)));
    }
    template<class Execute> void with_shader_module(resource_handle<wgpu::ShaderModule> handle,Execute execute) {
        check_thread();
        const auto& shader=shaders_.get(handle,owner_);
        struct guard { size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;} } scope(active_shader_scopes_);
        execute(shader);
    }
    void release_shader_module(resource_handle<wgpu::ShaderModule> handle) {
        check_thread();
        if(active_shader_scopes_)throw std::logic_error("Cannot release shader module during execution");
        shaders_.destroy(handle,owner_);
    }
    size_t live_shader_modules() const {check_thread();return shaders_.resident_count();}
    resource_handle<wgpu::RenderPipeline> create_render_pipeline(const wgpu::RenderPipelineDescriptor& descriptor) {
        const auto& device=native();
        if(!render_pipelines_.can_insert())throw std::length_error("Graphics render-pipeline capacity exhausted");
        auto pipeline=device.CreateRenderPipeline(&descriptor);
        if(!pipeline)throw std::runtime_error("Dawn did not return a render pipeline");
        return render_pipelines_.insert(owner_,std::make_unique<wgpu::RenderPipeline>(std::move(pipeline)));
    }
    template<class Execute> void with_render_pipeline(resource_handle<wgpu::RenderPipeline> handle,Execute execute) {
        check_thread();
        const auto& pipeline=render_pipelines_.get(handle,owner_);
        struct guard { size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;} } scope(active_render_pipeline_scopes_);
        execute(pipeline);
    }
    void release_render_pipeline(resource_handle<wgpu::RenderPipeline> handle) {
        check_thread();
        if(active_render_pipeline_scopes_)throw std::logic_error("Cannot release render pipeline during execution");
        render_pipelines_.destroy(handle,owner_);
    }
    size_t live_render_pipelines() const {check_thread();return render_pipelines_.resident_count();}
    resource_handle<wgpu::Texture> create_texture(const wgpu::TextureDescriptor& descriptor) {
        const auto& device=native();
        if(!textures_.can_insert())throw std::length_error("Graphics texture capacity exhausted");
        auto texture=device.CreateTexture(&descriptor);
        if(!texture)throw std::runtime_error("Dawn did not return a texture");
        return textures_.insert(owner_,std::make_unique<wgpu::Texture>(std::move(texture)));
    }
    // Host-only adoption of a texture imported/created on source_device. The
    // importer must supply the true source device; JavaScript cannot call this.
    // No texture allocation or pixel transfer occurs at this boundary.
    resource_handle<wgpu::Texture> adopt_texture(const wgpu::Device& source_device,wgpu::Texture texture) {
        const auto& device=native();
        if(!texture || source_device.Get()!=device.Get())throw std::invalid_argument("Imported texture requires its owning device");
        if(!textures_.can_insert())throw std::length_error("Graphics texture capacity exhausted");
        return textures_.insert(owner_,std::make_unique<wgpu::Texture>(std::move(texture)));
    }
    template<class Execute> void with_texture(resource_handle<wgpu::Texture> handle,Execute execute) {
        check_thread();
        const auto& texture=textures_.get(handle,owner_);
        struct guard { size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;} } scope(active_texture_scopes_);
        execute(texture);
    }
    void release_texture(resource_handle<wgpu::Texture> handle) {
        check_thread();
        if(active_texture_scopes_)throw std::logic_error("Cannot release texture during execution");
        textures_.destroy(handle,owner_);
    }
    size_t live_textures() const {check_thread();return textures_.resident_count();}
    resource_handle<wgpu::TextureView> create_texture_view(resource_handle<wgpu::Texture> texture,const wgpu::TextureViewDescriptor& descriptor) {
        native();
        if(!texture_views_.can_insert())throw std::length_error("Texture view capacity exhausted");
        auto view=textures_.get(texture,owner_).CreateView(&descriptor);
        if(!view)throw std::runtime_error("Dawn did not return a texture view");
        return texture_views_.insert(owner_,std::make_unique<wgpu::TextureView>(std::move(view)));
    }
    template<class Execute> void with_texture_view(resource_handle<wgpu::TextureView> handle,Execute execute) {
        check_thread();const auto& view=texture_views_.get(handle,owner_);
        struct guard {size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;}} scope(active_texture_view_scopes_);
        execute(view);
    }
    void release_texture_view(resource_handle<wgpu::TextureView> handle) {
        check_thread();if(active_texture_view_scopes_)throw std::logic_error("Cannot release a borrowed texture view");
        texture_views_.destroy(handle,owner_);
    }
    void destroy_texture(resource_handle<wgpu::Texture> handle) {
        check_thread();if(active_texture_scopes_ || active_texture_view_scopes_)throw std::logic_error("Cannot destroy a borrowed texture");
        textures_.get(handle,owner_).Destroy();
    }
    size_t live_texture_views() const {check_thread();return texture_views_.resident_count();}
    resource_handle<wgpu::CommandEncoder> create_command_encoder(const wgpu::CommandEncoderDescriptor& descriptor) {
        const auto& device=native();if(!command_encoders_.can_insert())throw std::length_error("Command encoder capacity exhausted");
        auto encoder=device.CreateCommandEncoder(&descriptor);if(!encoder)throw std::runtime_error("Dawn did not return a command encoder");
        return command_encoders_.insert(owner_,std::make_unique<wgpu::CommandEncoder>(std::move(encoder)));
    }
    resource_handle<wgpu::RenderPassEncoder> begin_render_pass(resource_handle<wgpu::CommandEncoder> encoder,const wgpu::RenderPassDescriptor& descriptor) {
        native();if(!render_passes_.can_insert())throw std::length_error("Render pass capacity exhausted");
        auto pass=command_encoders_.get(encoder,owner_).BeginRenderPass(&descriptor);if(!pass)throw std::runtime_error("Dawn did not return a render pass");
        return render_passes_.insert(owner_,std::make_unique<wgpu::RenderPassEncoder>(std::move(pass)));
    }
    resource_handle<wgpu::CommandBuffer> finish_command_encoder(resource_handle<wgpu::CommandEncoder> encoder,const wgpu::CommandBufferDescriptor& descriptor) {
        native();if(!command_buffers_.can_insert())throw std::length_error("Command buffer capacity exhausted");
        auto command=command_encoders_.get(encoder,owner_).Finish(&descriptor);if(!command)throw std::runtime_error("Dawn did not return a command buffer");
        return command_buffers_.insert(owner_,std::make_unique<wgpu::CommandBuffer>(std::move(command)));
    }
    template<class Execute> void with_command_encoder(resource_handle<wgpu::CommandEncoder> handle,Execute execute) {
        check_thread();const auto& resource=command_encoders_.get(handle,owner_);
        struct guard {size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;}} scope(active_command_scopes_);
        execute(resource);
    }
    void release_command_encoder(resource_handle<wgpu::CommandEncoder> handle) {
        check_thread();if(active_command_scopes_)throw std::logic_error("Cannot release command resources during execution");
        command_encoders_.destroy(handle,owner_);
    }
    size_t live_command_encoders() const {check_thread();return command_encoders_.resident_count();}
    template<class Execute> void with_render_pass(resource_handle<wgpu::RenderPassEncoder> handle,Execute execute) {
        check_thread();const auto& resource=render_passes_.get(handle,owner_);
        struct guard {size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;}} scope(active_command_scopes_);
        execute(resource);
    }
    void release_render_pass(resource_handle<wgpu::RenderPassEncoder> handle) {
        check_thread();if(active_command_scopes_)throw std::logic_error("Cannot release command resources during execution");
        render_passes_.destroy(handle,owner_);
    }
    size_t live_render_passes() const {check_thread();return render_passes_.resident_count();}
    template<class Execute> void with_command_buffer(resource_handle<wgpu::CommandBuffer> handle,Execute execute) {
        check_thread();const auto& resource=command_buffers_.get(handle,owner_);
        struct guard {size_t& count;explicit guard(size_t& value):count(value){++count;}~guard(){--count;}} scope(active_command_scopes_);
        execute(resource);
    }
    void release_command_buffer(resource_handle<wgpu::CommandBuffer> handle) {
        check_thread();if(active_command_scopes_)throw std::logic_error("Cannot release command resources during execution");
        command_buffers_.destroy(handle,owner_);
    }
    size_t live_command_buffers() const {check_thread();return command_buffers_.resident_count();}
    bool loss_pending() const {
        check_thread();
        return !closed_ && !lost_ && loss_ && loss_->lost.load(std::memory_order_acquire);
    }
    void process_loss() {
        check_thread();
        if (!loss_pending()) return;
        lost_=true;
        mailbox_->cancel_owner(owner_,completion_status::device_lost);
    }
    void close() {
        check_thread();
        if (closed_) return;
        if (active_buffer_scopes_ || active_shader_scopes_ || active_render_pipeline_scopes_ || active_texture_scopes_ || active_texture_view_scopes_ || active_command_scopes_) throw std::logic_error("Cannot close device during resource execution");
        process_loss();
        closed_=true;
        // Logical cancellation is independent of physical GPU completion.
        // Dawn retains submitted native resources until its backend is safe;
        // higher-level submission tables still require their completion fences.
        if (!lost_) mailbox_->cancel_owner(owner_);
        device_.Destroy();
        render_passes_.destroy_owner(owner_);
        command_encoders_.destroy_owner(owner_);
        command_buffers_.destroy_owner(owner_);
        buffers_.destroy_owner(owner_);
        shaders_.destroy_owner(owner_);
        render_pipelines_.destroy_owner(owner_);
        texture_views_.destroy_owner(owner_);
        textures_.destroy_owner(owner_);
    }
};
} // namespace webscene::graphics
