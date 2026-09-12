#pragma once
#include "webscene_css_stylesheet_data.h"
#include "webscene_css_rule_payload.h"
#include "webscene_css_rule_preparation.h"
#include "webscene_css_stylesheet_sink.h"

namespace webscene_native::css {
// Inventory reports whether a media condition is supported, not whether it
// currently matches. A false result records a diagnostic without dropping rules.
// This is the shared syntax/preparation stage, not the document cascade engine.
template<typename MediaInventory>
std::optional<prepared_stylesheet> prepare_stylesheet(std::string_view text,
    std::string address, MediaInventory&& inventory)
{
    struct preparation_host {
        prepared_stylesheet output;
        MediaInventory& inventory;
        std::mutex cache_mutex;
        rule_payload_cache cache;

        bool inventory_media_query(const std::string& query) { return inventory(query); }
        void record_feature(std::string_view, const std::string& feature,
            std::string_view classification, const std::string& detail, std::string_view) {
            // Font bytes/registration are handled by a resource host, which this
            // document-independent preparation stage deliberately does not own.
            if(feature=="at-rule:@font-face") {
                output.diagnostics.push_back({feature,"unsupported",
                    "font-face descriptors require resource-host preparation"});
            } else if(classification!="supported") {
                output.diagnostics.push_back({feature,std::string(classification),detail});
            }
        }
        void append_parsed_css_style_rule(std::string selector,
            std::vector<css_declaration> declarations,
            const std::vector<std::string>& media,const std::string& source) {
            const auto previous_count=output.rules.size();
            prepare_style_rule(selector,std::move(declarations),media,source,
                [](const css_declaration&) {},
                [&](const std::string& prepared,const auto& values,const auto& conditions) {
                    output.rules.push_back(intern_rule_payload(cache_mutex,cache,
                        compile_selector,prepared,values,conditions));
                });
            if(previous_count==output.rules.size())
                output.diagnostics.push_back({"selector:"+selector,"unsupported",
                    "selector parser rejected this rule"});
        }
    };
    preparation_host host{{std::move(address),{},{},{}},inventory,{}, {}};
    stylesheet_sink sink(host,host.output.source_address,text.size());
    if(!stream_css_syntax_stylesheet(text,sink) || !sink.complete()) return std::nullopt;
    for(auto& [name,definition]:sink.keyframes()) {
        finish_keyframes(host.output.keyframes,std::move(name),std::move(definition));
        host.output.diagnostics.push_back({"at-rule:@keyframes","partially-supported",
            "opacity and rotate() keyframes with host-clock timing"});
    }
    // A property known to WebScene must never cross the preparation boundary as
    // raw property grammar. Unknown properties remain explicit diagnostics, and
    // custom properties intentionally retain their CSS token stream for var().
    for(const auto& rule:host.output.rules) {
        for(const auto& declaration:rule->declarations) {
            if(declaration.property!=css_property_id::unknown && !declaration.has_typed_value()) {
                host.output.diagnostics.push_back({"property:"+declaration.name,"invalid-authoring",
                    "supported property did not compile to specified-value IR"});
                return std::nullopt;
            }
        }
    }
    return std::move(host.output);
}
} // namespace webscene_native::css
