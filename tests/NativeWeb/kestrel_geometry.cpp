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
    for(auto& fixture:fixtures["hatches"]){
        auto actual=kestrel::geo::hatch_segments(fixture["entity"]);auto& expected=fixture["segments"];
        if(actual.size()!=expected.size())throw std::runtime_error("Hatch segment count mismatch");
        for(size_t i=0;i<actual.size();++i)for(size_t j=0;j<2;++j)
            if((actual[i][j]-kestrel::geo::point(expected[i][j])).length()>1e-8)throw std::runtime_error("Hatch endpoint mismatch");
    }
    for(auto& fixture:fixtures["dimensions"]){
        auto actual=kestrel::geo::dimension(fixture["entity"]);auto& expected=fixture["geometry"];
        if(actual.segments.size()!=expected["segments"].size())throw std::runtime_error("Dimension segment count mismatch");
        for(size_t i=0;i<actual.segments.size();++i)for(size_t j=0;j<2;++j)
            if((actual.segments[i][j]-kestrel::geo::point(expected["segments"][i][j])).length()>1e-8)throw std::runtime_error("Dimension endpoint mismatch");
        for(auto key:{"position","direction","normal"})if((kestrel::geo::point(actual.text[key])-kestrel::geo::point(expected["text"][key])).length()>1e-8)throw std::runtime_error("Dimension text vector mismatch");
        for(auto key:{"height","rotation"})if(std::abs(actual.text[key].get<double>()-expected["text"][key].get<double>())>1e-8)throw std::runtime_error("Dimension text scalar mismatch");
        if(actual.text["text"]!=expected["text"]["text"]||actual.text["align"]!=expected["text"]["align"])throw std::runtime_error("Dimension label mismatch");
    }
    std::cout<<"Kestrel curves: "<<compared<<" upstream vertices matched\n";
}
