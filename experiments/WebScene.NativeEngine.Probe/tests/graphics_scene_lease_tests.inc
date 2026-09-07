// White-box fixture compiled only into the graphics runtime test executable.
// Exercises the production acquisition core and exported scene/lease operations.
void test_native_gpu_scene_leases()
{
    const auto require=[](bool value) { if (!value) throw std::runtime_error("GPU scene lease requirement failed"); };
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
    webscene_gpu_image_complete_consumer_v3(consumer); require(weak.expired());
}
