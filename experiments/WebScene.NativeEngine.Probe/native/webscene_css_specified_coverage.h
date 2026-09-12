#pragma once
#include "webscene_css_specified_ir.h"
#include <array>
#include <string_view>

namespace webscene_native::css {
struct specified_property_sample final { std::string_view name; std::string_view value; };

inline constexpr auto specified_property_samples = std::to_array<specified_property_sample>({
    {"all","unset"},{"content","\"x\""},
    {"width","12px"},{"height","20%"},{"min-width","1rem"},{"min-height","2px"},
    {"max-width","none"},{"max-height","fit-content"},
    {"left","1px"},{"top","2px"},{"right","3px"},{"bottom","4px"},{"inset","1px 2px 3px 4px"},
    {"padding","1px 2px 3px 4px"},{"padding-inline","1px 2px"},{"padding-block","3px 4px"},
    {"padding-left","1px"},{"padding-right","2px"},{"padding-top","3px"},{"padding-bottom","4px"},
    {"margin","1px 2px 3px auto"},{"margin-inline","auto 2px"},{"margin-block","1px 3px"},
    {"margin-left","auto"},{"margin-right","2px"},{"margin-top","3px"},{"margin-bottom","4px"},
    {"gap","4px 8px"},{"row-gap","normal"},{"column-gap","6px"},
    {"display","flex"},{"position","absolute"},{"contain","layout paint"},{"float","left"},{"z-index","7"},
    {"flex-direction","row-reverse"},{"flex-flow","column wrap"},{"flex-wrap","wrap"},
    {"flex-grow","2"},{"flex-shrink","0.5"},{"flex-basis","20px"},{"flex","1 0 20px"},
    {"align-items","center"},{"align-self","flex-end"},{"justify-content","space-between"},
    {"box-sizing","border-box"},{"vertical-align","middle"},
    {"grid-template-columns","1fr 20px"},{"grid-template-rows","auto 1fr"},{"grid-auto-columns","min-content"},
    {"grid-auto-flow","column"},{"grid-area","1 / 2 / 3 / 4"},{"grid-row","1 / 3"},
    {"grid-row-start","2"},{"grid-row-end","span 2"},{"grid-column","2 / 4"},
    {"grid-column-start","2"},{"grid-column-end","4"},
    {"border-spacing","2px 3px"},{"border-collapse","collapse"},{"table-layout","fixed"},
    {"border","1px solid red"},{"border-top","2px dashed blue"},{"border-right","1px solid green"},
    {"border-bottom","3px dotted black"},{"border-left","1px solid currentColor"},
    {"border-inline","1px solid red"},{"border-block","2px solid blue"},
    {"border-width","1px 2px 3px 4px"},{"border-color","red green blue black"},{"border-style","none"},
    {"border-top-width","1px"},{"border-right-width","2px"},{"border-bottom-width","3px"},{"border-left-width","4px"},
    {"border-inline-width","1px"},{"border-block-width","2px"},
    {"border-top-color","red"},{"border-right-color","green"},{"border-bottom-color","blue"},{"border-left-color","black"},
    {"border-inline-color","currentColor"},{"border-block-color","transparent"},
    {"border-radius","1px 2px 3px 4px"},{"border-top-left-radius","5px 6px"},
    {"border-top-right-radius","5px"},{"border-bottom-right-radius","7px"},{"border-bottom-left-radius","8px"},
    {"outline","1px solid red"},{"outline-width","2px"},{"outline-color","blue"},
    {"transform","translate(10px, 20px) scale(2) rotate(15deg)"},{"transform-origin","25% 75%"},
    {"transition","width 200ms ease 50ms"},{"transition-property","width, opacity"},
    {"transition-duration","200ms, 1s"},{"transition-delay","50ms"},{"transition-timing-function","cubic-bezier(.1,.2,.3,.4)"},
    {"animation","fade 1s linear 100ms infinite"},{"animation-name","fade"},{"animation-duration","1s"},
    {"animation-delay","100ms"},{"animation-timing-function","ease-in-out"},{"animation-iteration-count","infinite"},
    {"box-shadow","1px 2px 3px 4px rgba(0,0,0,.5)"},
    {"background","#123456"},{"background-color","rgba(1,2,3,.5)"},
    {"background-image","linear-gradient(90deg, red, blue)"},{"background-repeat","no-repeat"},
    {"background-position","center 20%"},{"background-size","cover"},
    {"overflow","auto hidden"},{"overflow-x","scroll"},{"overflow-y","clip"},
    {"visibility","hidden"},{"pointer-events","none"},{"opacity","0.5"},
    {"color","rebeccapurple"},{"fill","none"},{"stroke","currentColor"},{"stroke-width","2"},
    {"text-anchor","middle"},{"cursor","pointer"},
    {"font","700 16px/1.5 system-ui"},{"font-size","16px"},{"font-family","system-ui, sans-serif"},
    {"-webkit-font-smoothing","antialiased"},{"font-weight","700"},{"letter-spacing",".2px"},
    {"word-spacing","1px"},{"line-height","1.5"},{"text-align","center"},{"text-transform","uppercase"},
    {"white-space","nowrap"},{"list-style","inside square"},{"list-style-position","outside"},{"list-style-type","decimal"},
    {"scrollbar-width","thin"},{"scrollbar-color","#777 transparent"}
});

inline bool specified_ir_schema_complete()
{
    for(const auto& sample:specified_property_samples) {
        const auto property=property_id(sample.name);
        if(property==css_property_id::unknown || property==css_property_id::custom) return false;
        const auto compiled=compile_specified_value(property,sample.value);
        if(!compiled.fully_typed()) return false;
    }
    const auto custom=compile_specified_value("--webscene-test"," 1px solid red ");
    const auto deferred=compile_specified_value("width","calc(var(--size, 10px) + 2px)");
    return custom.kind==specified_css_kind::custom_tokens && custom.fully_typed()
        && deferred.kind==specified_css_kind::deferred && deferred.fully_typed();
}
} // namespace webscene_native::css
