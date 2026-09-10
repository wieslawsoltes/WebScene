#pragma once
// Ported from upstream src/math.js; see math.hpp and ../LICENSE.
#include "math.hpp"
#include <string_view>
#include <stdexcept>
#include <cstdint>
namespace kestrel {
struct camera_state {vec3 target{};double zoom=1,yaw=-std::numbers::pi/2,pitch=std::numbers::pi/2;bool perspective=false;};
class camera : public camera_state {
public:
    double width=1000,height=700,distance{};
    uint64_t revision{};
    vec3 right,up,direction,eye;
    kestrel::matrix view{},projection{},combined{};
    std::optional<kestrel::matrix> inverse_matrix;
    camera(){update();}
    void update(){
        auto c=std::cos(pitch);direction={std::cos(yaw)*c,std::sin(yaw)*c,std::sin(pitch)};
        right={-std::sin(yaw),std::cos(yaw),0};up=direction.cross(right);
        auto h=height/zoom;distance=h*1.35;eye=target+direction*distance;
        auto r=right,u=up,n=direction,e=eye;
        view={r.x,u.x,n.x,0,r.y,u.y,n.y,0,r.z,u.z,n.z,0,-r.dot(e),-u.dot(e),-n.dot(e),1};
        auto w=width/zoom,near=std::max(.00001,distance/100000),far=distance*100;
        projection=perspective?kestrel::perspective(40*std::numbers::pi/180,width/height,near,far):ortho(-w/2,w/2,-h/2,h/2,near,far);
        combined=multiply(projection,view);inverse_matrix=inverse(combined);++revision;
    }
    void resize(double w,double h){if(w==width&&h==height)return;width=w;height=h;update();}
    vec3 project(vec3 p)const{
        auto& m=combined;auto w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];if(w<=0)return {-1e9,-1e9,2};
        auto v=transform(m,p);return {(v.x+1)*width/2,(1-v.y)*height/2,v.z};
    }
    struct ray_value{vec3 origin,direction;};
    ray_value ray(double x,double y)const{
        if(!inverse_matrix)throw std::runtime_error("Camera projection is singular");
        auto a=transform(*inverse_matrix,{x/width*2-1,1-y/height*2,0});
        auto b=transform(*inverse_matrix,{x/width*2-1,1-y/height*2,1});return {a,(b-a).normalized()};
    }
    std::optional<vec3> unproject(double x,double y,double z=0)const{
        auto r=ray(x,y);if(std::abs(r.direction.z)<1e-7)return {};return r.origin+r.direction*((z-r.origin.z)/r.direction.z);
    }
    vec3 point_on_view(double x,double y)const{auto r=ray(x,y);return r.origin+r.direction*((target-r.origin).dot(direction)/r.direction.dot(direction));}
    void pan(double dx,double dy){target=target-(point_on_view(width/2+dx,height/2+dy)-point_on_view(width/2,height/2));update();}
    void zoom_at(double factor,double x,double y){
        auto before=unproject(x,y,target.z).value_or(point_on_view(x,y));zoom=std::clamp(zoom*factor,1e-7,1e7);update();
        auto after=unproject(x,y,target.z).value_or(point_on_view(x,y));target=target+before-after;update();
    }
    void zoom_at(double factor){zoom_at(factor,width/2,height/2);}
    void set_view(std::string_view name){
        double y=-48,p=32;
        if(name=="top"){y=-90;p=90;}else if(name=="bottom"){y=-90;p=-90;}
        else if(name=="front"){y=-90;p=0;}else if(name=="back"){y=90;p=0;}
        else if(name=="right"){y=0;p=0;}else if(name=="left"){y=180;p=0;}
        yaw=y*std::numbers::pi/180;pitch=p*std::numbers::pi/180;update();
    }
    void fit(std::span<const vec3> points,double padding=1.22){
        if(points.empty()){target={};zoom=std::min(width/2000,height/1400);update();return;}
        vec3 min=points.front(),max=min;
        for(auto p:points){min={std::min(min.x,p.x),std::min(min.y,p.y),std::min(min.z,p.z)};max={std::max(max.x,p.x),std::max(max.y,p.y),std::max(max.z,p.z)};}
        target=(min+max)*.5;double x=0,y=0;
        for(auto p:points){auto d=p-target;x=std::max(x,std::abs(d.dot(right)));y=std::max(y,std::abs(d.dot(up)));}
        zoom=std::min(width/std::max(x*2*padding,1.),height/std::max(y*2*padding,1.));update();
    }
    camera_state serialize()const{return *this;}
    void restore(camera_state state){target=state.target;if(std::isfinite(state.zoom))zoom=state.zoom;
        if(std::isfinite(state.yaw))yaw=state.yaw;if(std::isfinite(state.pitch))pitch=state.pitch;perspective=state.perspective;update();}
};
}
