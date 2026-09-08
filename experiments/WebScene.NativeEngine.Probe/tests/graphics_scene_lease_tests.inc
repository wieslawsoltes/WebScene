// White-box fixture compiled only into the graphics runtime test executable.
// Exercises the production acquisition core and exported scene/lease operations.
#include <source_location>
void test_native_gpu_scene_leases()
{
    const auto require=[](bool value, std::source_location location=std::source_location::current()) {
        if (!value) {
            const auto message="GPU scene lease requirement failed at line " + std::to_string(location.line());
            std::fprintf(stderr,"%s\n",message.c_str());
            throw std::runtime_error(message);
        }
    };
    // Exercise the production commit boundary with deliberately delayed outputs.
    // This constructor disables the worker only in this test executable.
    {
        webscene_engine engine(nullptr);
        struct test_provider final : webscene::graphics::image_provider_lifetime {};
        webscene::graphics::owned_image_pool pool(std::make_shared<test_provider>());
        struct delayed_output final : webscene_gpu_image_snapshot {
            std::shared_ptr<const webscene_gpu_image_lease_v3> image;
            status completion=status::pending;
            webscene::graphics::image_metadata describe()const override{return image->value.describe();}
            status state()const override{return completion;}
            std::shared_ptr<const webscene_gpu_image_lease_v3> resolve()override {
                return completion==status::ready?image:nullptr;
            }
        };
        auto& canvas=engine.document_.create_element("canvas");
        require(engine.document_.append_child(engine.document_.body(),canvas));
        const auto& backing=canvas.mutable_canvas().backing;
        require(canvas.mutable_canvas().backing.claim_context(webscene::graphics::canvas_context_mode::webgpu));
        auto writer=pool.acquire();
        writer->set_metadata({backing.identity(),100,backing.allocation_generation(),backing.content_serial(),1,1,backing.width(),backing.height()});
        writer->begin();auto retained=writer->publish();writer->complete();writer.reset();
        auto output=std::make_shared<delayed_output>();
        output->image=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*retained));retained.reset();
        const auto make_capture=[&](uint64_t revision,uint64_t base) {
            auto value=std::make_shared<scene>();
            value->header.revision=revision;value->header.base_revision=base;
            value->header.viewport_width=static_cast<float>(engine.viewport_width_);
            value->header.viewport_height=static_cast<float>(engine.viewport_height_);
            value->captured_generation=engine.document_.scene_generation();
            value->gpu_bindings.push_back({canvas.id,output->describe(),output,{}});
            value->gpu_images.resize(1);
            value->commands.push_back({});value->commands.back().x=static_cast<float>(revision);
            return value;
        };
        const auto commit=[&](auto value){return engine.commit_captured_scene(value,std::chrono::steady_clock::now());};
        using result=webscene_engine::publication_result;
        canvas.mutable_canvas().gpu_snapshot=output;
        engine.document_.mark_scene_changed(); // Rendering opportunity submission.
        const auto submitted_generation=engine.document_.scene_generation();
        output->completion=webscene_gpu_image_snapshot::status::ready;
        engine.document_.publish_gpu_canvas_image(canvas,output->image);
        require(engine.document_.scene_generation()==submitted_generation);
        // A completed output without a captured dependency must still invalidate.
        canvas.mutable_canvas().gpu_snapshot.reset();canvas.mutable_canvas().gpu_image.reset();
        engine.document_.publish_gpu_canvas_image(canvas,output->image);
        require(engine.document_.scene_generation()==submitted_generation+1);
        auto a=make_capture(1,0);output->completion=webscene_gpu_image_snapshot::status::ready;
        require(commit(a)==result::published);
        engine.acknowledgement_->pending_scenes.clear();engine.acknowledgement_->revision=1;
        auto b=make_capture(2,1);output->completion=webscene_gpu_image_snapshot::status::pending;
        require(commit(b)==result::deferred && engine.latest_->header.revision==1);
        engine.document_.mark_scene_changed();
        auto c=make_capture(3,2); // Newer CPU capture must not replace frozen B.
        require(commit(b)==result::deferred && engine.latest_->commands[0].x==1);
        output->completion=webscene_gpu_image_snapshot::status::ready;
        require(commit(b)==result::published && engine.latest_->commands[0].x==2);
        require(engine.published_document_generation_!=engine.document_.scene_generation());
        require(commit(c)==result::deferred); // Mailbox is full; input capture remains owned.
        engine.acknowledgement_->pending_scenes.clear();engine.acknowledgement_->revision=2;
        require(commit(c)==result::published && engine.latest_->commands[0].x==3);
        engine.acknowledgement_->pending_scenes.clear();engine.acknowledgement_->revision=3;
        auto failed=make_capture(4,3);output->completion=webscene_gpu_image_snapshot::status::failed;
        require(commit(failed)==result::discarded && engine.latest_->header.revision==3);
        output->completion=webscene_gpu_image_snapshot::status::ready;
        auto missing=make_capture(4,3);
        auto missing_output=std::make_shared<webscene_failed_gpu_image_snapshot>(output->describe());
        canvas.mutable_canvas().gpu_snapshot=missing_output;
        canvas.mutable_canvas().gpu_image=output->image; // Older completed image must not be substituted.
        engine.document_.layout(engine.viewport_width_,engine.viewport_height_);
        engine.document_.build_gpu_canvas_bindings(missing->gpu_bindings);
        require(missing->gpu_bindings.size()==1 && missing->gpu_bindings[0].pending==missing_output);
        require(!missing->gpu_bindings[0].resolve());
        require(commit(missing)==result::discarded && engine.latest_->header.revision==3);
        canvas.mutable_canvas().gpu_snapshot=output;
        auto recovery=make_capture(4,3);
        require(commit(recovery)==result::published && engine.latest_->header.revision==4);
        engine.acknowledgement_->pending_scenes.clear();engine.acknowledgement_->revision=4;
        auto stale=make_capture(5,2);
        require(commit(stale)==result::deferred && !engine.staged_scene_ && engine.latest_->header.revision==4);
        auto resized=make_capture(6,4);engine.viewport_width_+=1;
        require(commit(resized)==result::deferred && engine.latest_->header.revision==4);
        engine.viewport_width_-=1;
        auto reset=make_capture(7,4);
        canvas.mutable_canvas().backing.reset_bitmap(backing.width(),backing.height());
        require(commit(reset)==result::deferred && engine.latest_->header.revision==4);
    }
    // Full production capture + commit: A remains published while B is pending,
    // even after live DOM and GPU state advance to C. No manual scene assembly.
    {
        webscene_engine engine(nullptr);
        engine.native_scene_active_=true;
        engine.ordered_scene_consumer_.store(true);
        struct capture_provider final : webscene::graphics::image_provider_lifetime {};
        webscene::graphics::owned_image_pool pool(std::make_shared<capture_provider>());
        struct captured_output final : webscene_gpu_image_snapshot {
            std::shared_ptr<const webscene_gpu_image_lease_v3> image;
            bool ready=false;
            webscene::graphics::image_metadata describe()const override{return image->value.describe();}
            status state()const override{return ready?status::ready:status::pending;}
            std::shared_ptr<const webscene_gpu_image_lease_v3> resolve()override{return ready?image:nullptr;}
        };
        auto& node=engine.document_.create_element("canvas");
        require(engine.document_.append_child(engine.document_.body(),node));
        auto& backing=node.mutable_canvas().backing;
        require(backing.claim_context(webscene::graphics::canvas_context_mode::webgpu));
        const auto submit=[&](uint32_t color) {
            backing.publish_content();
            auto writer=pool.acquire();require(writer.has_value());
            writer->set_metadata({backing.identity(),100+backing.content_serial(),backing.allocation_generation(),backing.content_serial(),1,1,backing.width(),backing.height()});
            writer->begin();auto image=writer->publish();writer->complete();writer.reset();
            auto output=std::make_shared<captured_output>();
            output->image=std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image));
            node.mutable_canvas().gpu_snapshot=output;
            engine.document_.body().style.background_rgba=color;
            engine.document_.mark_dirty();
            return output;
        };
        const auto check=[&](uint64_t serial,uint32_t color) {
            require(engine.latest_ && engine.latest_->gpu_images.size()==1);
            require(engine.latest_->gpu_images[0]->value.describe().content_serial==serial);
            require(std::any_of(engine.latest_->commands.begin(),engine.latest_->commands.end(),
                [&](const auto& command){return command.node_id==engine.document_.body().id && command.rgba==color;}));
        };
        const auto acknowledge=[&] {
            const webscene_scene_view_v3* view=nullptr;
            require(acquire_scene_value_v3(engine.latest_,engine.acknowledgement_,
                WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES|WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS,&view)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
            require(webscene_scene_acknowledge_v3(view));webscene_scene_release_v3(view);
        };
        using result=webscene_engine::publication_result;
        auto a=submit(0xff0000ff);a->ready=true;
        require(engine.publish_scene()==result::published);check(1,0xff0000ff);acknowledge();
        auto b=submit(0x00ff00ff);
        require(engine.publish_scene()==result::deferred);check(1,0xff0000ff);
        auto c=submit(0x0000ffff);
        require(engine.publish_scene()==result::deferred);check(1,0xff0000ff);
        b->ready=true;
        require(engine.publish_scene()==result::published);check(2,0x00ff00ff);
        require(engine.published_document_generation_!=engine.document_.scene_generation());
        acknowledge();
        require(engine.publish_scene()==result::deferred);check(2,0x00ff00ff);
        c->ready=true;
        require(engine.publish_scene()==result::published);check(3,0x0000ffff);acknowledge();
    }
    // Ordered Canvas2D placement is independently negotiated, including a diff
    // whose unchanged command payload has been stripped after capability hashing.
    auto ordered=std::make_shared<scene>();
    ordered->required_capabilities=scene_command_capabilities(WEBSCENE_SCENE_COMMAND_CANVAS_LAYER);
    require(ordered->required_capabilities==WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS);
    require(scene_command_capabilities(1)==0);
    require(scene_command_capabilities(WEBSCENE_SCENE_COMMAND_GPU_IMAGE)==WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES);
    const webscene_scene_view_v3* ordered_view=nullptr;
    for (const uint64_t offered : {uint64_t(0), uint64_t(WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES)}) {
        require(acquire_scene_value_v3(ordered,{},offered,&ordered_view)==
            WEBSCENE_SCENE_ACQUIRE_UNSUPPORTED_CAPABILITIES && !ordered_view);
    }
    require(acquire_scene_value_v3(ordered,{},WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS,&ordered_view)==
        WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    require(ordered_view->required_capabilities==WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS);
    webscene_scene_release_v3(ordered_view);
    ordered->required_capabilities |= scene_command_capabilities(WEBSCENE_SCENE_COMMAND_GPU_IMAGE);
    require(acquire_scene_value_v3(ordered,{},WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS,&ordered_view)==
        WEBSCENE_SCENE_ACQUIRE_UNSUPPORTED_CAPABILITIES && !ordered_view);
    require(acquire_scene_value_v3(ordered,{},WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS |
        WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES,&ordered_view)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    webscene_scene_release_v3(ordered_view);
    struct provider final : webscene::graphics::image_provider_lifetime {};
    auto backend=std::make_shared<provider>();
    auto weak=std::weak_ptr<provider>(backend);
    auto pool=std::make_unique<webscene::graphics::owned_image_pool>(backend); backend.reset();
    auto writer=pool->acquire();
    writer->set_metadata({1,2,1,1,3,1,64,64}); writer->begin();
    auto image=writer->publish(); writer->complete(); writer.reset();
    auto first=std::make_shared<scene>();
    first->header.revision=1;
    first->header.flags=scene_flag_checkpoint | scene_flag_dom_replacement;
    first->gpu_images.push_back(std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image))); image.reset();
    first->commands.push_back({WEBSCENE_SCENE_COMMAND_GPU_IMAGE,0,0,0,64,64,0,1,0,0,0,0,0});
    auto acknowledgement=std::make_shared<acknowledgement_state>();
    acknowledgement->pending_scenes.push_back(first);
    const webscene_scene_view_v3* view=nullptr;
    require(acquire_scene_value_v3(first,acknowledgement,0,&view)==WEBSCENE_SCENE_ACQUIRE_UNSUPPORTED_CAPABILITIES && !view);
    require(acknowledgement->pending_scenes.size()==1 && acknowledgement->revision==0);
    require(acquire_scene_value_v3(first,acknowledgement,WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES,&view)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    require(view->required_capabilities==WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES && webscene_scene_gpu_image_count_v3(view)==1);
    webscene_gpu_image_lease_v3* retained=nullptr;
    require(webscene_scene_retain_gpu_image_v3(view,1,&retained)==WEBSCENE_SCENE_ACQUIRE_INVALID_ARGUMENT && !retained);
    require(webscene_scene_retain_gpu_image_v3(view,0,&retained)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    webscene_gpu_image_consumer_v3* consumer=nullptr;
    require(webscene_gpu_image_begin_consumer_v3(retained,&consumer)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    auto removed=std::make_shared<scene>();
    removed->header.revision=2; removed->header.base_revision=1;
    removed->header.flags=scene_flag_dom_replacement;
    acknowledgement->pending_scenes.push_back(removed);
    const webscene_scene_view_v3* premature=nullptr;
    require(acquire_scene_value_v3(removed,acknowledgement,0,&premature)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    require(!webscene_scene_acknowledge_v3(premature) && acknowledgement->revision==0);
    webscene_scene_release_v3(premature);
    require(webscene_scene_acknowledge_v3(view) && acknowledgement->pending_scenes.size()==1);
    webscene_scene_release_v3(view); first.reset();
    // A later ordered scene drops the GPU image. Both independent CPU retention and
    // already-submitted consumer work must survive acknowledgement of removal.
    require(acquire_scene_value_v3(removed,acknowledgement,0,&view)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    require(webscene_scene_gpu_image_count_v3(view)==0 && webscene_scene_acknowledge_v3(view));
    webscene_scene_release_v3(view); removed.reset();
    auto stale=std::make_shared<scene>();
    stale->header.revision=3; stale->header.base_revision=1; // Current base is 2.
    stale->gpu_images.push_back(std::make_shared<webscene_gpu_image_lease_v3>(retained->value.retain().value()));
    acknowledgement->pending_scenes.push_back(stale);
    const webscene_scene_view_v3* stale_view=nullptr;
    require(acquire_scene_value_v3(stale,acknowledgement,WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES,&stale_view)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    require(!webscene_scene_acknowledge_v3(stale_view) && acknowledgement->revision==2);
    acknowledgement->reset_for_checkpoint(); // Same reset used by the engine API.
    require(acknowledgement->pending_scenes.empty() && acknowledgement->revision==0);
    require(webscene_scene_gpu_image_count_v3(stale_view)==1);
    auto recovery=std::make_shared<scene>();
    recovery->header.revision=4;
    recovery->header.flags=scene_flag_checkpoint | scene_flag_dom_replacement;
    recovery->gpu_images=stale->gpu_images;
    acknowledgement->pending_scenes.push_back(recovery);
    require(acquire_scene_value_v3(recovery,acknowledgement,WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES,&view)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    require(webscene_scene_acknowledge_v3(view) && acknowledgement->revision==4);
    require(!webscene_scene_acknowledge_v3(stale_view));
    webscene_scene_release_v3(stale_view); stale.reset();
    webscene_scene_release_v3(view); recovery.reset(); acknowledgement.reset(); pool.reset();
    webscene_gpu_image_info_v3 info{}; info.struct_size=sizeof(info); info.version=3;
    require(webscene_gpu_image_describe_v3(retained,&info) && info.content_serial==1 && !weak.expired());
    webscene_gpu_image_release_v3(retained); require(!weak.expired());
    webscene_gpu_iosurface_view_v3 native_view{sizeof(native_view),3,nullptr,0};
    require(!webscene_gpu_image_get_iosurface_v3(consumer,&native_view) && !native_view.borrowed_iosurface);
    webscene_gpu_image_complete_consumer_v3(consumer); require(weak.expired());
#if defined(__APPLE__)
    auto surfaces=std::make_unique<webscene::graphics::iosurface_canvas_images>(1024*1024);
    auto surface=surfaces->acquire({700,0,1,1,701,1,17,4,webscene::graphics::image_format::bgra8_unorm});
    require(surface.has_value());
    surface->producer.begin();
    auto published=surface->producer.publish();
    surface->producer.complete(); surface.reset();
    require(published.has_value());
    auto surface_lease=std::make_unique<webscene_gpu_image_lease_v3>(std::move(*published));
    webscene_gpu_image_consumer_v3* surface_consumer=nullptr;
    require(webscene_gpu_image_begin_consumer_v3(surface_lease.get(),&surface_consumer)==WEBSCENE_SCENE_ACQUIRE_SUCCESS);
    surface_lease.reset(); surfaces.reset();
    require(webscene_gpu_image_get_iosurface_v3(surface_consumer,&native_view) &&
        native_view.borrowed_iosurface && native_view.allocation_bytes>=17*4*4);
    require(IOSurfaceGetWidth(static_cast<IOSurfaceRef>(native_view.borrowed_iosurface))==17);
    native_view.version=2;
    require(!webscene_gpu_image_get_iosurface_v3(surface_consumer,&native_view));
    native_view.version=3; native_view.struct_size=0;
    require(!webscene_gpu_image_get_iosurface_v3(surface_consumer,&native_view));
    native_view.struct_size=sizeof(native_view);
    require(!webscene_gpu_image_get_iosurface_v3(nullptr,&native_view) &&
        !native_view.borrowed_iosurface && !native_view.allocation_bytes);
    webscene_gpu_image_complete_consumer_v3(surface_consumer);
#endif
}
