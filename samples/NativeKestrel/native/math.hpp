#pragma once
// Ported from KestrelCAD src/math.js at 7a1e84c67fd24410c22f0d1a45b2e54120b32d0e.
// Copyright (c) 2026 Kestrel CAD contributors. MIT; see ../LICENSE.
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <span>
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
}
