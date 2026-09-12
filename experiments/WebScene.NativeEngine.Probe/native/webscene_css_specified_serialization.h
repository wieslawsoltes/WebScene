#pragma once
#include "webscene_css_specified_ir.h"
#include <bit>
#include <cstring>
#include <limits>

namespace webscene_native::css {
namespace specified_serialization {
inline void u8(std::string& out,uint8_t v){out.push_back(static_cast<char>(v));}
inline void u32(std::string& out,uint32_t v){for(unsigned i=0;i<4;++i)out.push_back(static_cast<char>((v>>(i*8U))&0xffU));}
inline void i32(std::string& out,int32_t v){u32(out,std::bit_cast<uint32_t>(v));}
inline void f32(std::string& out,float v){u32(out,std::bit_cast<uint32_t>(v));}
inline void string(std::string& out,std::string_view v){u32(out,static_cast<uint32_t>(v.size()));out.append(v);}
inline void length(std::string& out,const css_length& v){f32(out,v.value);u8(out,static_cast<uint8_t>(v.unit));f32(out,v.pixel_offset);}
inline void color(std::string& out,const css_color_value& v){u32(out,v.rgba);u8(out,v.current_color);u8(out,v.none);u8(out,v.valid);}
inline void component(std::string& out,const css_typed_component& v){u8(out,static_cast<uint8_t>(v.kind));string(out,v.name);f32(out,v.number);length(out,v.length);u32(out,static_cast<uint32_t>(v.arguments.size()));for(const auto& a:v.arguments)component(out,a);}
inline void components(std::string& out,const std::vector<css_typed_component>& v){u32(out,static_cast<uint32_t>(v.size()));for(const auto& c:v)component(out,c);}

struct reader final{
    std::string_view data;size_t at{};bool ok{true};
    uint8_t u8(){if(at>=data.size()){ok=false;return 0;}return static_cast<uint8_t>(data[at++]);}
    uint32_t u32(){if(data.size()-std::min(at,data.size())<4U){ok=false;return 0;}uint32_t v{};for(unsigned i=0;i<4;++i)v|=uint32_t(static_cast<uint8_t>(data[at++]))<<(i*8U);return v;}
    int32_t i32(){return std::bit_cast<int32_t>(u32());}
    float f32(){return std::bit_cast<float>(u32());}
    std::string string(){const auto n=u32();if(!ok||n>data.size()-std::min(at,data.size())){ok=false;return{};}auto v=std::string(data.substr(at,n));at+=n;return v;}
    css_length length(){css_length v;v.value=f32();v.unit=static_cast<length_unit>(u8());v.pixel_offset=f32();return v;}
    css_color_value color(){css_color_value v;v.rgba=u32();v.current_color=u8()!=0;v.none=u8()!=0;v.valid=u8()!=0;return v;}
    css_typed_component component(unsigned depth=0){css_typed_component v;if(depth>32U){ok=false;return v;}v.kind=static_cast<css_typed_component_kind>(u8());v.name=string();v.number=f32();v.length=length();const auto n=u32();if(n>4096U){ok=false;return v;}v.arguments.reserve(n);for(uint32_t i=0;i<n&&ok;++i)v.arguments.push_back(component(depth+1U));return v;}
    std::vector<css_typed_component> components(){std::vector<css_typed_component> v;const auto n=u32();if(n>65536U){ok=false;return v;}v.reserve(n);for(uint32_t i=0;i<n&&ok;++i)v.push_back(component());return v;}
};
}

inline std::string encode_specified_value(const specified_css_value& value){
    using namespace specified_serialization;std::string out;out.reserve(64);u8(out,1);u8(out,static_cast<uint8_t>(value.kind));u8(out,static_cast<uint8_t>(value.wide));u8(out,value.valid);
    switch(value.kind){
    case specified_css_kind::invalid:case specified_css_kind::wide_keyword:break;
    case specified_css_kind::keyword:string(out,value.keyword);break;
    case specified_css_kind::number:f32(out,value.number);break;
    case specified_css_kind::integer:i32(out,value.integer);u8(out,value.automatic);break;
    case specified_css_kind::length:length(out,value.length);string(out,value.keyword);break;
    case specified_css_kind::length_list:u8(out,value.length_count);for(unsigned i=0;i<value.length_count&&i<4U;++i){length(out,value.lengths[i]);string(out,value.length_keywords[i]);}break;
    case specified_css_kind::color:color(out,value.color);break;
    case specified_css_kind::color_list:u8(out,value.color_count);for(unsigned i=0;i<value.color_count&&i<4U;++i)color(out,value.colors[i]);break;
    case specified_css_kind::border:length(out,value.border.width);color(out,value.border.color);string(out,value.border.style);u8(out,value.border.width_specified);u8(out,value.border.color_specified);u8(out,value.border.style_specified);u8(out,value.border.none);break;
    case specified_css_kind::transform:length(out,value.transform.translate_x);length(out,value.transform.translate_y);f32(out,value.transform.scale_x);f32(out,value.transform.scale_y);f32(out,value.transform.rotate_degrees);u8(out,value.transform.none);break;
    case specified_css_kind::transform_origin:length(out,value.transform_origin.x);length(out,value.transform_origin.y);break;
    case specified_css_kind::track_list:u8(out,value.tracks.subgrid);u8(out,value.tracks.multiple);u32(out,static_cast<uint32_t>(value.tracks.tracks.size()));for(const auto& track:value.tracks.tracks){length(out,track.minimum);length(out,track.maximum);f32(out,track.fraction);u8(out,static_cast<uint8_t>(track.kind));}break;
    case specified_css_kind::grid_placement:u32(out,static_cast<uint32_t>(value.grid_components.size()));for(const auto& v:value.grid_components)string(out,v);break;
    case specified_css_kind::flex_flow:u8(out,static_cast<uint8_t>(value.flex_flow.direction));u8(out,value.flex_flow.reverse);u8(out,value.flex_flow.wrap);break;
    case specified_css_kind::flex:f32(out,value.flex.grow);f32(out,value.flex.shrink);length(out,value.flex.basis);u8(out,value.flex.basis_auto);u8(out,value.flex.none);break;
    case specified_css_kind::overflow:u8(out,static_cast<uint8_t>(value.overflow.x));u8(out,static_cast<uint8_t>(value.overflow.y));break;
    case specified_css_kind::background:color(out,value.background.color);u8(out,value.background.has_color);u8(out,static_cast<uint8_t>(value.background.image_kind));string(out,value.background.url);components(out,value.background.image_components);break;
    case specified_css_kind::shadow:u8(out,value.shadow.length_count);for(unsigned i=0;i<value.shadow.length_count&&i<4U;++i)length(out,value.shadow.lengths[i]);color(out,value.shadow.color);u8(out,value.shadow.color_specified);u8(out,value.shadow.inset);u8(out,value.shadow.multiple);u8(out,value.shadow.valid);break;
    case specified_css_kind::font:components(out,value.components);break;
    case specified_css_kind::list_style:string(out,value.list_style.position);string(out,value.list_style.type);break;
    case specified_css_kind::content:string(out,value.content.decoded);u8(out,value.content.generated);break;
    case specified_css_kind::component_list:components(out,value.components);break;
    case specified_css_kind::deferred:u32(out,static_cast<uint32_t>(value.deferred.segments.size()));for(const auto& s:value.deferred.segments){u8(out,static_cast<uint8_t>(s.type));string(out,s.text);string(out,s.fallback);}break;
    case specified_css_kind::custom_tokens:string(out,value.token_data);break;
    }
    return out;
}

inline specified_css_value decode_specified_value(std::string_view bytes){
    using namespace specified_serialization;reader r{bytes};specified_css_value value;if(r.u8()!=1U){r.ok=false;return value;}value.kind=static_cast<specified_css_kind>(r.u8());value.wide=static_cast<css_wide_keyword>(r.u8());value.valid=r.u8()!=0;
    switch(value.kind){
    case specified_css_kind::invalid:case specified_css_kind::wide_keyword:break;
    case specified_css_kind::keyword:value.keyword=r.string();break;
    case specified_css_kind::number:value.number=r.f32();break;
    case specified_css_kind::integer:value.integer=r.i32();value.automatic=r.u8()!=0;break;
    case specified_css_kind::length:value.length=r.length();value.keyword=r.string();break;
    case specified_css_kind::length_list:value.length_count=r.u8();if(value.length_count>4U)r.ok=false;for(unsigned i=0;i<value.length_count&&r.ok;++i){value.lengths[i]=r.length();value.length_keywords[i]=r.string();}break;
    case specified_css_kind::color:value.color=r.color();break;
    case specified_css_kind::color_list:value.color_count=r.u8();if(value.color_count>4U)r.ok=false;for(unsigned i=0;i<value.color_count&&r.ok;++i)value.colors[i]=r.color();break;
    case specified_css_kind::border:value.border.width=r.length();value.border.color=r.color();value.border.style=r.string();value.border.width_specified=r.u8()!=0;value.border.color_specified=r.u8()!=0;value.border.style_specified=r.u8()!=0;value.border.none=r.u8()!=0;break;
    case specified_css_kind::transform:value.transform.translate_x=r.length();value.transform.translate_y=r.length();value.transform.scale_x=r.f32();value.transform.scale_y=r.f32();value.transform.rotate_degrees=r.f32();value.transform.none=r.u8()!=0;break;
    case specified_css_kind::transform_origin:value.transform_origin.x=r.length();value.transform_origin.y=r.length();break;
    case specified_css_kind::track_list:{value.tracks.subgrid=r.u8()!=0;value.tracks.multiple=r.u8()!=0;const auto n=r.u32();if(n>65536U){r.ok=false;break;}value.tracks.tracks.reserve(n);for(uint32_t i=0;i<n&&r.ok;++i){node_style::grid_data::track track;track.minimum=r.length();track.maximum=r.length();track.fraction=r.f32();track.kind=static_cast<node_style::grid_data::track::sizing>(r.u8());value.tracks.tracks.push_back(track);}break;}
    case specified_css_kind::grid_placement:{const auto n=r.u32();if(n>64U){r.ok=false;break;}for(uint32_t i=0;i<n&&r.ok;++i)value.grid_components.push_back(r.string());break;}
    case specified_css_kind::flex_flow:value.flex_flow.direction=static_cast<flex_direction>(r.u8());value.flex_flow.reverse=r.u8()!=0;value.flex_flow.wrap=r.u8()!=0;break;
    case specified_css_kind::flex:value.flex.grow=r.f32();value.flex.shrink=r.f32();value.flex.basis=r.length();value.flex.basis_auto=r.u8()!=0;value.flex.none=r.u8()!=0;break;
    case specified_css_kind::overflow:value.overflow.x=static_cast<overflow_mode>(r.u8());value.overflow.y=static_cast<overflow_mode>(r.u8());break;
    case specified_css_kind::background:value.background.color=r.color();value.background.has_color=r.u8()!=0;value.background.image_kind=static_cast<css_background_image_kind>(r.u8());value.background.url=r.string();value.background.image_components=r.components();break;
    case specified_css_kind::shadow:value.shadow.length_count=r.u8();if(value.shadow.length_count>4U)r.ok=false;for(unsigned i=0;i<value.shadow.length_count&&r.ok;++i)value.shadow.lengths[i]=r.length();value.shadow.color=r.color();value.shadow.color_specified=r.u8()!=0;value.shadow.inset=r.u8()!=0;value.shadow.multiple=r.u8()!=0;value.shadow.valid=r.u8()!=0;break;
    case specified_css_kind::font:value.components=r.components();break;
    case specified_css_kind::list_style:value.list_style.position=r.string();value.list_style.type=r.string();break;
    case specified_css_kind::content:value.content.decoded=r.string();value.content.generated=r.u8()!=0;break;
    case specified_css_kind::component_list:value.components=r.components();break;
    case specified_css_kind::deferred:{const auto n=r.u32();if(n>4096U){r.ok=false;break;}for(uint32_t i=0;i<n&&r.ok;++i){css_deferred_segment s;s.type=static_cast<css_deferred_segment::kind>(r.u8());s.text=r.string();s.fallback=r.string();value.deferred.segments.push_back(std::move(s));}break;}
    case specified_css_kind::custom_tokens:value.token_data=r.string();break;
    }
    if(!r.ok||r.at!=bytes.size()){value={};}
    return value;
}

} // namespace webscene_native::css
