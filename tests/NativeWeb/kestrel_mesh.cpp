#include "../../samples/NativeKestrel/native/geometry.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=2)return 2;std::ifstream input(argv[1]);kestrel::json fixture;input>>fixture;
    using namespace kestrel;using namespace kestrel::geo;
    auto meshes=std::array{box({1,2,3},4,5,6),cylinder({1,2,3},4,5,12),cylinder({1,2,3},4,5,12,0),sphere({1,2,3},4,12,6),torus({1,2,3},8,2,12,6)};
    for(size_t i=0;i<meshes.size();++i){auto& a=meshes[i];auto& b=fixture["meshes"][i];
        if(a["faces"]!=b["faces"]||a["primitive"]!=b["primitive"]||a["vertices"].size()!=b["vertices"].size())throw std::runtime_error("Mesh topology mismatch");
        for(size_t j=0;j<a["vertices"].size();++j)if((point(a["vertices"][j])-point(b["vertices"][j])).length()>1e-10)throw std::runtime_error("Mesh vertex mismatch");
    }
    std::cout<<"Kestrel: five upstream mesh primitives matched\n";
}
