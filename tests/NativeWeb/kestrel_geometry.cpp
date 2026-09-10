#include "../../samples/NativeKestrel/native/geometry.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=2)return 2;std::ifstream input(argv[1]);kestrel::json fixtures;input>>fixtures;
    size_t compared=0;
    for(auto& fixture:fixtures["cases"]){auto actual=kestrel::geo::path(fixture["entity"],fixture["tolerance"]);auto expected=kestrel::geo::points(fixture["path"]);
        if(actual.size()!=expected.size())throw std::runtime_error("Tessellation count mismatch: "+fixture["name"].get<std::string>());
        for(size_t i=0;i<actual.size();++i){if((actual[i]-expected[i]).length()>1e-8)throw std::runtime_error("Tessellation vertex mismatch");++compared;}
        if(kestrel::geo::closed(fixture["entity"])!=fixture["closed"].get<bool>())throw std::runtime_error("Closed path mismatch");
    }
    std::cout<<"Kestrel curves: "<<compared<<" upstream vertices matched\n";
}
