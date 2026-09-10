#include "../../samples/NativeKestrel/native/math.hpp"
#include <stdexcept>
int main(){
    using namespace kestrel;
    auto m=multiply(translation({10,-3,5}),multiply(rotation(0.8,{1,2,3}),scale({2,3,4})));
    auto inv=inverse(m);if(!inv)throw std::runtime_error("invertible transform rejected");
    for(auto p:{vec3{0,0,0},vec3{100,-40,2},vec3{1e5,2e5,-3e5}})
        if((transform(*inv,transform(m,p))-p).length()>1e-8)throw std::runtime_error("transform round trip");
    if(inverse(scale({1,0,1})))throw std::runtime_error("singular transform accepted");
    auto cross=line_intersection({0,0,0},{10,10,0},{0,10,0},{10,0,0});
    if(!cross||(cross->point-vec3{5,5,0}).length()>epsilon)throw std::runtime_error("line intersection");
    if(line_intersection({0,0,0},{10,0,0},{0,2,0},{10,2,0}))throw std::runtime_error("parallel intersection");
    std::array<vec3,4> polygon{{{0,0,0},{10,0,0},{10,4,0},{0,4,0}}};
    if(polygon_area(polygon)!=40||std::abs(sweep(0,0)-tau)>epsilon)throw std::runtime_error("area/sweep");
}
