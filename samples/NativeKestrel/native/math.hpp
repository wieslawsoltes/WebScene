#pragma once
// Ported from KestrelCAD src/math.js at 7a1e84c67fd24410c22f0d1a45b2e54120b32d0e.
// Copyright (c) 2026 Kestrel CAD contributors. MIT; see ../LICENSE.
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <span>
#include <vector>
#include <numeric>
namespace kestrel {
inline constexpr double epsilon=1e-8, tau=2*std::numbers::pi;
struct vec3 {
    double x{},y{},z{};
    vec3 operator+(vec3 b) const {return {x+b.x,y+b.y,z+b.z};}
    vec3 operator-(vec3 b) const {return {x-b.x,y-b.y,z-b.z};}
    vec3 operator*(double s) const {return {x*s,y*s,z*s};}
    double dot(vec3 b) const {return x*b.x+y*b.y+z*b.z;}
    vec3 cross(vec3 b) const {return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
    double length() const {return std::hypot(x,y,z);}
    vec3 normalized() const {auto l=length();return l>epsilon?*this*(1/l):vec3{};}
};
inline vec3 lerp(vec3 a,vec3 b,double t){return a+(b-a)*t;}
using matrix=std::array<double,16>;
inline matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
inline matrix multiply(const matrix& a,const matrix& b){
    matrix r{};for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)
        r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;
}
inline vec3 transform(const matrix& m,vec3 p,double w=1){
    vec3 q{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12]*w,
           m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13]*w,
           m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]*w};
    auto divisor=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15]*w;
    return w&&std::abs(divisor)>epsilon?q*(1/divisor):q;
}
inline matrix translation(vec3 p){auto m=identity();m[12]=p.x;m[13]=p.y;m[14]=p.z;return m;}
inline matrix scale(vec3 p){auto m=identity();m[0]=p.x;m[5]=p.y;m[10]=p.z;return m;}
inline matrix rotation(double angle,vec3 axis={0,0,1}){
    auto [x,y,z]=axis.normalized();auto c=std::cos(angle),s=std::sin(angle),t=1-c;
    return {t*x*x+c,t*x*y+s*z,t*x*z-s*y,0,t*x*y-s*z,t*y*y+c,t*y*z+s*x,0,
        t*x*z+s*y,t*y*z-s*x,t*z*z+c,0,0,0,0,1};
}
inline matrix around(vec3 p,const matrix& m){return multiply(translation(p),multiply(m,translation(p*-1)));}
inline std::optional<matrix> inverse(const matrix& a){
    std::array<std::array<double,8>,4> aug{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){aug[r][c]=a[c*4+r];aug[r][c+4]=r==c;}
    for(int i=0;i<4;++i){
        int k=i;for(int r=i+1;r<4;++r)if(std::abs(aug[r][i])>std::abs(aug[k][i]))k=r;
        if(std::abs(aug[k][i])<1e-16)return {};
        std::swap(aug[i],aug[k]);auto s=aug[i][i];for(auto& v:aug[i])v/=s;
        for(int r=0;r<4;++r)if(r!=i){auto f=aug[r][i];for(int c=0;c<8;++c)aug[r][c]-=f*aug[i][c];}
    }
    matrix result{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)result[c*4+r]=aug[r][c+4];return result;
}
inline double angle(double a){return std::fmod(std::fmod(a,tau)+tau,tau);}
inline double sweep(double a,double b){auto s=angle(b-a);return s<epsilon?tau:s;}
struct intersection {vec3 point;double t,u;};
inline std::optional<intersection> line_intersection(vec3 a,vec3 b,vec3 c,vec3 d){
    auto r=b-a,s=d-c;auto den=r.x*s.y-r.y*s.x;if(std::abs(den)<epsilon)return {};
    auto u=c-a;auto t=(u.x*s.y-u.y*s.x)/den,v=(u.x*r.y-u.y*r.x)/den;
    return intersection{lerp(a,b,t),t,v};
}
inline double polygon_area(std::span<const vec3> points){
    double area=0;if(points.empty())return 0;
    for(size_t i=0,j=points.size()-1;i<points.size();j=i++)area+=points[j].x*points[i].y-points[i].x*points[j].y;
    return area/2;
}

inline matrix ortho(double l,double r,double b,double t,double n,double f){
    auto m=identity();m[0]=2/(r-l);m[5]=2/(t-b);m[10]=1/(n-f);
    m[12]=-(r+l)/(r-l);m[13]=-(t+b)/(t-b);m[14]=n/(n-f);return m;
}
inline matrix perspective(double fov,double aspect,double n,double f){
    matrix m{};auto t=1/std::tan(fov/2);m[0]=t/aspect;m[5]=t;m[10]=f/(n-f);m[11]=-1;m[14]=n*f/(n-f);return m;
}
struct coordinate_basis {vec3 x,y,n;};
inline coordinate_basis basis(vec3 normal={0,0,1}){
    auto n=normal.normalized();auto x=(std::abs(n.x)<1.0/64&&std::abs(n.y)<1.0/64?vec3{0,1,0}:vec3{0,0,1}).cross(n).normalized();
    return {x,n.cross(x),n};
}
struct segment_result {double distance,t;vec3 point;};
inline segment_result segment_distance(vec3 p,vec3 a,vec3 b){
    auto dx=b.x-a.x,dy=b.y-a.y,l=dx*dx+dy*dy;
    auto t=l?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/l,0.,1.):0.;
    return {std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy),t,{a.x+t*dx,a.y+t*dy,0}};
}
inline bool inside(vec3 p,std::span<const vec3> polygon){
    bool yes=false;if(polygon.empty())return false;
    for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
        auto a=polygon[i],b=polygon[j];
        if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)yes=!yes;
    }return yes;
}
inline vec3 face_normal(std::span<const vec3> points){
    vec3 n;for(size_t i=0;i<points.size();++i){auto a=points[i],b=points[(i+1)%points.size()];
        n.x+=(a.y-b.y)*(a.z+b.z);n.y+=(a.z-b.z)*(a.x+b.x);n.z+=(a.x-b.x)*(a.y+b.y);
    }return n.normalized();
}
inline std::vector<std::array<size_t,3>> triangulate(std::span<const vec3> points){
    if(points.size()<3)return {};if(points.size()==3)return {{{0,1,2}}};
    auto n=face_normal(points);auto drop=std::abs(n.x)>std::abs(n.y)?(std::abs(n.x)>std::abs(n.z)?0:2):(std::abs(n.y)>std::abs(n.z)?1:2);
    std::vector<vec3> p;for(auto v:points)p.push_back(drop==0?vec3{v.y,v.z,0}:drop==1?vec3{v.x,v.z,0}:vec3{v.x,v.y,0});
    double sign=polygon_area(p)>=0?1:-1;std::vector<size_t> indices(p.size());std::iota(indices.begin(),indices.end(),0);
    std::vector<std::array<size_t,3>> out;
    auto cross=[](vec3 a,vec3 b,vec3 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
    size_t guard=points.size()*points.size();
    while(indices.size()>3&&guard--){
        bool found=false;
        for(size_t k=0;k<indices.size();++k){
            auto a=indices[(k+indices.size()-1)%indices.size()],b=indices[k],c=indices[(k+1)%indices.size()];
            if(cross(p[a],p[b],p[c])*sign<=epsilon)continue;
            bool blocked=false;for(auto j:indices){if(j==a||j==b||j==c)continue;
                if(cross(p[a],p[b],p[j])*sign>=-epsilon&&cross(p[b],p[c],p[j])*sign>=-epsilon&&cross(p[c],p[a],p[j])*sign>=-epsilon){blocked=true;break;}}
            if(!blocked){out.push_back({a,b,c});indices.erase(indices.begin()+k);found=true;break;}
        }
        if(!found){
            size_t k=0;for(;k<indices.size();++k)if(std::abs(cross(p[indices[(k+indices.size()-1)%indices.size()]],p[indices[k]],p[indices[(k+1)%indices.size()]]))<epsilon)break;
            if(k<indices.size())indices.erase(indices.begin()+k);else break;
        }
    }
    if(indices.size()==3)out.push_back({indices[0],indices[1],indices[2]});return out;
}
}
