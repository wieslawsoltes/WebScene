#include "webscene_native_engine.h"
#include <stddef.h>
_Static_assert(sizeof(webscene_scene_acquire_options_v3)==16,"scene options wire size");
_Static_assert(offsetof(webscene_scene_acquire_options_v3,consumer_capabilities)==8,"scene capability alignment");
_Static_assert(sizeof(webscene_scene_acquire_status)==4,"scene status wire size");
_Static_assert(offsetof(webscene_scene_view_v3,required_capabilities)==8,"scene capability prefix");
_Static_assert(offsetof(webscene_scene_view_v3,cpu_view)==16,"scene CPU view prefix");
_Static_assert(offsetof(webscene_scene_view_v3,lease_token)==16+sizeof(void*),"scene lease offset");
_Static_assert(sizeof(webscene_scene_view_v3)==16+2*sizeof(void*),"scene view wire size");
_Static_assert(sizeof(webscene_gpu_image_info_v3)==80,"image metadata wire size");
_Static_assert(offsetof(webscene_gpu_image_info_v3,canvas)==8,"image identity alignment");
_Static_assert(offsetof(webscene_gpu_image_info_v3,width)==56,"image dimensions offset");
int main(void) { return WEBSCENE_SCENE_VIEW_VERSION_3==3U ? 0 : 1; }
