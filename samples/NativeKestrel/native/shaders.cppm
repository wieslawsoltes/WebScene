module;
#include <string_view>
export module kestrel.shaders;
// WGSL from KestrelCAD renderer.js at the pinned revision in ../README.md.
// Upstream MIT attribution: ../LICENSE. This is shader code, not JavaScript.
export namespace kestrel {
inline constexpr std::string_view shader_source = R"wgsl(
 struct Camera { mvp: mat4x4f, eye: vec4f, viewport: vec4f };
 @group(0) @binding(0) var<uniform> camera: Camera;
 struct LineOut { @builtin(position) position: vec4f, @location(0) color: vec4f,
   @location(1) local: vec2f, @location(2) length: f32, @location(3) halfWidth: f32, @location(4) dash: f32 };
 @vertex fn lineVertex(@builtin(vertex_index) vi: u32, @location(0) a: vec3f,
   @location(1) b: vec3f, @location(2) color: vec4f, @location(3) params: vec2f) -> LineOut {
   var pa = camera.mvp * vec4f(a, 1.0); var pb = camera.mvp * vec4f(b, 1.0);
   if (pa.z < 0.0 && pb.z < 0.0) { pa = vec4f(4.0, 4.0, 2.0, 1.0); pb = pa; }
   else if (pa.z < 0.0) { pa = mix(pa, pb, pa.z / (pa.z-pb.z)); }
   else if (pb.z < 0.0) { pb = mix(pb, pa, pb.z / (pb.z-pa.z)); }
   let delta = (pb.xy / pb.w - pa.xy / pa.w) * camera.viewport.xy * 0.5;
   let len = max(length(delta), 0.0001); let dir = delta / len;
   let normal = vec2f(-dir.y, dir.x); let halfWidth = max(params.x * 0.5, 0.25);
   let extent = halfWidth + 1.0;
   let corners = array<vec2f,6>(vec2f(0.,-1.),vec2f(1.,-1.),vec2f(1.,1.),vec2f(0.,-1.),vec2f(1.,1.),vec2f(0.,1.));
   let corner = corners[vi]; let t = corner.x; var p = mix(pa,pb,t);
   let shift = normal*corner.y*extent + dir*(t*2.0-1.0)*extent;
   p = vec4f(p.xy + shift * 2.0 / camera.viewport.xy * p.w,p.z,p.w);
   var o: LineOut; o.position=p; o.color=color; o.local=vec2f(mix(-extent,len+extent,t),corner.y*extent);
   o.length=len; o.halfWidth=halfWidth; o.dash=params.y; return o;
 }
 @fragment fn lineFragment(i: LineOut) -> @location(0) vec4f {
   if (i.dash > 0.5) { let d = i.local.x % 22.0;
     if (i.dash < 1.5 && d > 14.0) { discard; }
     if (i.dash > 1.5 && ((d > 12.0 && d < 16.0) || d > 18.0)) { discard; }
   }
   let outside = max(max(-i.local.x,i.local.x-i.length),0.0);
   let distance = length(vec2f(outside,i.local.y))-i.halfWidth;
   let alpha=clamp(0.5-distance,0.0,1.0)*i.color.a;
   if (alpha < 0.01) { discard; } return vec4f(i.color.rgb,alpha);
 }
 struct MeshOut { @builtin(position) position: vec4f, @location(0) color: vec4f,
   @location(1) normal: vec3f, @location(2) world: vec3f };
 @vertex fn meshVertex(@location(0) p: vec3f,@location(1) normal: vec3f,@location(2) color: vec4f) -> MeshOut {
   var o: MeshOut; o.position=camera.mvp*vec4f(p,1.0);o.color=color;o.normal=normal;o.world=p;return o;
 }
 @fragment fn meshFragment(i: MeshOut,@builtin(front_facing) front: bool) -> @location(0) vec4f {
   var n=normalize(i.normal); if(!front){ n=-n; }
   let light=normalize(vec3f(-0.35,-0.45,0.85));let view=normalize(camera.eye.xyz-i.world);
   let diffuse=max(dot(n,light),0.0);let fill=max(dot(n,normalize(vec3f(0.7,0.2,0.3))),0.0);
   let spec=pow(max(dot(reflect(-light,n),view),0.0),42.0)*0.18;
   let intensity=0.38+diffuse*0.53+fill*0.16;
   let shaded=i.color.rgb*intensity+vec3f(spec);
   return vec4f(mix(i.color.rgb,shaded,camera.viewport.z),i.color.a);
 })wgsl";
}
