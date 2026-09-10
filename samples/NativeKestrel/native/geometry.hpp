#pragma once
// Ported from KestrelCAD src/geometry.js at the README's pinned revision.
#include "math.hpp"
#include "drawing.hpp"
namespace kestrel::geo {
inline vec3 point(const json& p){return {p.at(0).get<double>(),p.at(1).get<double>(),p.size()>2?p.at(2).get<double>():0};}
inline std::vector<vec3> points(const json& array){std::vector<vec3> out;for(auto& p:array)out.push_back(point(p));return out;}
inline vec3 vector_property(const json& e,const char* key,vec3 fallback){return e.contains(key)&&!e[key].is_null()?point(e[key]):fallback;}
inline double number(const json& e,const char* key,double fallback){return e.contains(key)&&!e[key].is_null()?e[key].get<double>():fallback;}
struct conic_axes {vec3 x,y;};
inline conic_axes axes(const json& e){
    if(e.contains("axisX")&&e.contains("axisY"))return {point(e["axisX"]),point(e["axisY"])};
    auto b=basis(vector_property(e,"normal",{0,0,1}));auto r=number(e,"rotation",0);
    return {(b.x*std::cos(r)+b.y*std::sin(r))*number(e,"rx",number(e,"radius",1)),
            (b.x*-std::sin(r)+b.y*std::cos(r))*number(e,"ry",number(e,"radius",1))};
}
inline vec3 conic_point(const json& e,double t){auto a=axes(e);return point(e.at("center"))+a.x*std::cos(t)+a.y*std::sin(t);}
inline std::vector<double> uniform_knots(size_t count,size_t degree){
    std::vector<double> out;for(size_t i=0;i<count+degree+1;++i)out.push_back(i<=degree?0:i>=count?1:double(i-degree)/(count-degree));return out;
}
inline vec3 nurbs(const json& e,double t){
    auto p=points(e.contains("controlPoints")?e["controlPoints"]:e.at("points"));
    if(p.size()<2)throw std::invalid_argument("NURBS requires at least two points");
    size_t degree=std::min<size_t>(e.value("degree",3),p.size()-1);
    if(!degree)throw std::invalid_argument("NURBS degree must be positive");
    auto knots=e.contains("knots")&&e["knots"].size()==p.size()+degree+1?e["knots"].get<std::vector<double>>():uniform_knots(p.size(),degree);
    auto weights=e.contains("weights")?e["weights"].get<std::vector<double>>():std::vector<double>{};
    size_t k=degree;auto lo=knots[degree],hi=knots[p.size()],u=lo+(hi-lo)*std::clamp(t,0.,1.);
    while(k<p.size()-1&&knots[k+1]<=u)++k;
    std::vector<std::array<double,4>> d;
    for(size_t j=0;j<=degree;++j){auto i=k-degree+j;double w=i<weights.size()?weights[i]:1;d.push_back({p[i].x*w,p[i].y*w,p[i].z*w,w});}
    for(size_t r=1;r<=degree;++r)for(size_t j=degree;j>=r;--j){auto i=k-degree+j;auto den=knots[i+degree-r+1]-knots[i],a=den>0?(u-knots[i])/den:0;
        for(size_t c=0;c<4;++c)d[j][c]=(1-a)*d[j-1][c]+a*d[j][c];}
    auto w=d[degree][3]?d[degree][3]:1;return {d[degree][0]/w,d[degree][1]/w,d[degree][2]/w};
}
inline std::vector<vec3> polyline_points(const json& e,double tolerance=1){
    auto p=e.contains("points")?points(e["points"]):std::vector<vec3>{};std::vector<vec3> out;
    for(size_t i=0;i<p.size();++i){out.push_back(p[i]);if(i==p.size()-1&&!e.value("closed",false))break;
        auto b=e.contains("bulges")&&i<e["bulges"].size()?e["bulges"][i].get<double>():0.;if(std::abs(b)<epsilon)continue;
        auto a=p[i],z=p[(i+1)%p.size()];auto chord=(z-a).length();if(chord<epsilon)continue;
        auto normal=vector_property(e,"normal",{0,0,1}),left=normal.cross(z-a).normalized();
        auto center=lerp(a,z,.5)+left*(chord*(1-b*b)/(4*b));auto radius=chord*(1+b*b)/(4*std::abs(b)),delta=4*std::atan(b);
        int steps=int(std::clamp(std::ceil(std::abs(delta)*std::sqrt(radius/std::max(.01,tolerance))),4.,256.));
        for(int k=1;k<steps;++k)out.push_back(center+transform(rotation(delta*k/steps,normal),a-center,0));
    }return out;
}
inline std::vector<vec3> path(const json& e,double tolerance=.5){
    auto type=e.at("type").get<std::string>();
    if(type=="LINE"||type=="HATCH")return points(e.at("points"));
    if(type=="POLYLINE")return polyline_points(e,tolerance);
    if(type=="CIRCLE"||type=="ARC"||type=="ELLIPSE"){
        auto a=number(e,"startAngle",0),d=type=="ARC"||(type=="ELLIPSE"&&e.contains("endAngle")&&!e["endAngle"].is_null())?sweep(a,e.at("endAngle").get<double>()):tau;
        auto ax=axes(e);auto r=std::max(ax.x.length(),ax.y.length());
        int n=int(std::clamp(std::ceil(d*std::sqrt(r/std::max(.001,tolerance))),24.,512.));
        std::vector<vec3> out;for(int i=0;i<n+(d<tau-epsilon?1:0);++i)out.push_back(conic_point(e,a+d*i/n));return out;
    }
    if(type=="SPLINE"){
        auto count=e.contains("controlPoints")?e["controlPoints"].size():e.contains("points")?e["points"].size():0;
        if(count<2)return {};auto n=std::min<size_t>(512,std::max<size_t>(32,count*20));
        std::vector<vec3> out;for(size_t i=0;i<=n;++i)out.push_back(nurbs(e,double(i)/n));return out;
    }
    if(type=="POINT"||type=="TEXT")return {point(e.at("position"))};return {};
}
inline bool closed(const json& e){auto t=e.at("type");return t=="CIRCLE"||t=="HATCH"||(t=="POLYLINE"&&e.value("closed",false))||
    (t=="ELLIPSE"&&(!e.contains("endAngle")||e["endAngle"].is_null()||sweep(number(e,"startAngle",0),e["endAngle"].get<double>())>tau-epsilon));}

inline json encode(vec3 p){return {p.x,p.y,p.z};}
inline json box(vec3 p,double w,double d,double h){
    json v=json::array();for(auto q:{vec3{0,0,0},vec3{w,0,0},vec3{w,d,0},vec3{0,d,0},vec3{0,0,h},vec3{w,0,h},vec3{w,d,h},vec3{0,d,h}})v.push_back(encode(p+q));
    return {{"type","MESH"},{"primitive","Box"},{"vertices",v},{"faces",{{3,2,1,0},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}}}};
}
inline json cylinder(vec3 p,double radius,double height,int n=64,std::optional<double> top_radius={}){
    if(n<3)throw std::invalid_argument("Cylinder requires at least three segments");
    double top=top_radius.value_or(radius);json vertices=json::array(),faces=json::array();
    for(int j=0;j<2;++j)for(int i=0;i<n;++i){auto a=double(i)/n*tau,r=j?top:radius;vertices.push_back(encode(p+vec3{std::cos(a)*r,std::sin(a)*r,j*height}));}
    json bottom=json::array(),upper=json::array();for(int i=0;i<n;++i){bottom.push_back(n-i-1);upper.push_back(n+i);}faces.push_back(bottom);if(top>epsilon)faces.push_back(upper);
    for(int i=0;i<n;++i)faces.push_back({i,(i+1)%n,(i+1)%n+n,i+n});
    return {{"type","MESH"},{"primitive",top==radius?"Cylinder":"Cone"},{"vertices",vertices},{"faces",faces}};
}
inline json sphere(vec3 p,double r,int n=40,int rings=20){
    if(n<3||rings<2)throw std::invalid_argument("Invalid sphere subdivisions");
    json vertices=json::array(),faces=json::array();vertices.push_back(encode(p+vec3{0,0,-r}));
    for(int j=1;j<rings;++j){auto t=-std::numbers::pi/2+double(j)/rings*std::numbers::pi;
        for(int i=0;i<n;++i)vertices.push_back(encode(p+vec3{r*std::cos(t)*std::cos(double(i)/n*tau),r*std::cos(t)*std::sin(double(i)/n*tau),r*std::sin(t)}));}
    auto top=vertices.size();vertices.push_back(encode(p+vec3{0,0,r}));
    for(int i=0;i<n;++i){faces.push_back({0,1+(i+1)%n,1+i});for(int j=0;j<rings-2;++j){auto a=1+j*n+i,b=1+j*n+(i+1)%n;faces.push_back({a,b,b+n,a+n});}faces.push_back({1+(rings-2)*n+i,1+(rings-2)*n+(i+1)%n,int(top)});}
    return {{"type","MESH"},{"primitive","Sphere"},{"vertices",vertices},{"faces",faces},{"smooth",true}};
}
inline json torus(vec3 p,double major,double minor,int n=64,int m=20){
    if(n<3||m<3)throw std::invalid_argument("Invalid torus subdivisions");
    json vertices=json::array(),faces=json::array();
    for(int i=0;i<n;++i)for(int j=0;j<m;++j){auto a=double(i)/n*tau,b=double(j)/m*tau;vertices.push_back(encode(p+vec3{(major+minor*std::cos(b))*std::cos(a),(major+minor*std::cos(b))*std::sin(a),minor*std::sin(b)}));}
    for(int i=0;i<n;++i)for(int j=0;j<m;++j)faces.push_back({i*m+j,((i+1)%n)*m+j,((i+1)%n)*m+(j+1)%m,i*m+(j+1)%m});
    return {{"type","MESH"},{"primitive","Torus"},{"vertices",vertices},{"faces",faces},{"smooth",true}};
}
}
