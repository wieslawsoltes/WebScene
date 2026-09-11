#pragma once
#include "webscene_css_stylesheet_data.h"
#include "webscene_css_rule_index.h"
#include "webscene_css_candidates.h"
#include "webscene_css_rule_operations.h"
#include "webscene_css_media.h"

namespace webscene_native::css {
// Owns prepared author sheets. No CSS text parsing is performed here. Rule views
// remain valid until replace/remove; the document host owns recascade scheduling.
class stylesheet_owner {
    struct entry { uint32_t id; prepared_stylesheet sheet; };
    std::vector<entry> sheets_;
    css_cascade_state state_;
    media_environment environment_{0,0,false};
    bool rule_media_matches(const css_rule& rule) const {
        return std::all_of(rule.media_queries().begin(),rule.media_queries().end(),
            [&](const auto& query) { return css::media_matches(query,environment_); });
    }
    void rebuild() {
        state_=css_cascade_state{};
        for(const auto& entry:sheets_) {
            for(const auto& payload:entry.sheet.rules) {
                auto index=state_.rules.size();
                state_.rules.push_back({payload,entry.id,0,true});
                auto& rule=state_.rules.back();
                rule.media_matches=rule_media_matches(rule);
                if(rule.selector().find("::selection")==std::string::npos)
                    index_selector(index,rule.selector(),state_.rules_by_id,state_.rules_by_class,
                        state_.rules_by_tag,state_.rules_by_attribute,state_.focus_rules,
                        state_.unindexed_rules,state_.descendant_attribute_dependencies);
            }
            for(const auto& [name,definition]:entry.sheet.keyframes)
                state_.opacity_keyframes[name]=definition;
        }
    }
public:
    // Replacing preserves sheet source order; newly attached sheets append.
    void replace(uint32_t id,prepared_stylesheet sheet) {
        for(auto& entry:sheets_) if(entry.id==id) {
            entry.sheet=std::move(sheet); rebuild(); return;
        }
        sheets_.push_back({id,std::move(sheet)}); rebuild();
    }
    bool remove(uint32_t id) {
        const auto old_size=sheets_.size();
        std::erase_if(sheets_,[&](const auto& entry) { return entry.id==id; });
        if(old_size==sheets_.size()) return false;
        rebuild(); return true;
    }
    // Returns whether rule activation changed, allowing the host to recascade.
    bool set_environment(media_environment environment) {
        environment_=environment;
        bool changed=false;
        for(auto& rule:state_.rules) {
            auto matches=rule_media_matches(rule);
            changed|=matches!=rule.media_matches;
            rule.media_matches=matches;
        }
        return changed;
    }
    const css_cascade_state& state() const noexcept { return state_; }
    std::vector<size_t> candidates(const dom_node& node,bool focused=false) const {
        auto result=collect_candidates(node,focused,state_.rules_by_tag,state_.rules_by_id,
            state_.rules_by_attribute,state_.focus_rules,state_.unindexed_rules,
            [&](auto& output,std::string_view name) {
                auto found=state_.rules_by_class.find(std::string(name));
                if(found!=state_.rules_by_class.end())
                    output.insert(output.end(),found->second.begin(),found->second.end());
            });
        sort_candidates(state_.rules,result);
        result.erase(std::unique(result.begin(),result.end()),result.end());
        return result;
    }
};
} // namespace webscene_native::css
