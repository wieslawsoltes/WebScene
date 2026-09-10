#pragma once
#include "webscene_css_compound.h"

namespace webscene_native::css {
// Borrowed document service: the document outlives this host, and all calls occur
// on its owning thread. Cache entries contain prepared syntax, never node results.
class query_host final {
    uint32_t hover_id_{},focus_id_{};
    bool focus_visible_{};
    std::string hash_;
    size_t cache_limit_;
    mutable std::unordered_map<std::string,std::shared_ptr<const compiled_css_selector_list>> selectors_;
    std::shared_ptr<const compiled_css_selector_list> prepare(std::string_view text) const {
        const std::string key(text);
        if(auto found=selectors_.find(key);found!=selectors_.end()) return found->second;
        auto value=std::make_shared<const compiled_css_selector_list>(compile_selector_list(text));
        if(selectors_.size()>=cache_limit_) selectors_.clear();
        if(cache_limit_) selectors_.emplace(key,value);
        return value;
    }
    bool matches(const dom_node& node,const compiled_css_selector_list& list,const dom_node* scope) const {
        for(const auto& selector:list.selectors)
            if(matches_prepared(node,selector,scope)) return true;
        return false;
    }
    dom_node* find(dom_node& node,const compiled_css_selector_list& list,bool include,
        const dom_node* scope) const {
        if(include && !node.tag.empty() && node.tag.front()!='#' && matches(node,list,scope)) return &node;
        for(auto* child:node.children) if(child)
            if(auto* result=find(*child,list,true,scope)) return result;
        return nullptr;
    }
public:
    native_document& document;
    explicit query_host(native_document& value,size_t cache_limit=256)
        :cache_limit_(cache_limit),document(value) {}
    void set_interaction(const dom_node* hover,const dom_node* focus,bool visible) {
        hover_id_=hover?hover->id:0;focus_id_=focus?focus->id:0;focus_visible_=visible;
    }
    void set_target_hash(std::string hash) { hash_=std::move(hash); }
    interaction_state selector_interaction_state() const {
        return {document.find_by_native_id(hover_id_),document.find_by_native_id(focus_id_),focus_visible_};
    }
    std::optional<std::string> selector_target_hash() const { return hash_; }
    bool is_text_control(const dom_node* node) const { return forms::is_text_control(node); }
    bool has_class(const dom_node& node,std::string_view wanted) const {
        const std::string_view text(node.class_name);
        constexpr std::string_view space=" \t\n\f\r";
        size_t offset=0;
        while((offset=text.find_first_not_of(space,offset))!=std::string_view::npos) {
            const auto end=text.find_first_of(space,offset);
            if(text.substr(offset,end==std::string_view::npos?end:end-offset)==wanted) return true;
            if(end==std::string_view::npos) break;
            offset=end;
        }
        return false;
    }
    bool css_selector_matches(const dom_node& node,std::string_view text,const dom_node* scope=nullptr) const {
        // Pin syntax across recursive :is/:not/:has calls that can evict the cache.
        const auto prepared=prepare(text);
        return matches(node,*prepared,scope);
    }
    // Reuse the stylesheet's prepared selector without reparsing its outer syntax.
    // Nested functional selectors retain the same bounded cache as DOM queries.
    bool matches_prepared(const dom_node& node,const compiled_css_selector& selector,
        const dom_node* scope=nullptr) const {
        return !selector.compounds.empty() && selector_matches(document,node,selector,
            selector.compounds.size()-1,scope,[&](const auto& n,const auto& c,const auto* root) {
                return compound_matches(*this,n,c,root);
            });
    }
    dom_node* query_selector_node(dom_node& root,std::string_view text,bool include_root=false) const {
        const auto prepared=prepare(text);
        return find(root,*prepared,include_root,&root);
    }
    void clear_cache() { selectors_.clear(); }
};
} // namespace webscene_native::css
