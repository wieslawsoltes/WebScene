#pragma once
#include "webscene_native_dom.h"

namespace webscene_native::forms {
inline void collect_descendants_by_tag(
        dom_node& root,
        std::string_view tag,
        std::vector<dom_node*>& result)
    {
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (child->tag == tag) result.push_back(child);
            collect_descendants_by_tag(*child, tag, result);
        }
    }

inline dom_node* containing_select(dom_node& option)
    {
        auto* select = option.parent;
        while (select != nullptr && select->tag != "select") select = select->parent;
        return select;
    }

inline bool option_is_disabled(const dom_node& option)
    {
        if (option.attributes.contains("disabled")) return true;
        for (auto* ancestor = option.parent;
             ancestor != nullptr && ancestor->tag != "select";
             ancestor = ancestor->parent) {
            if (ancestor->tag == "optgroup"
                && ancestor->attributes.contains("disabled")) return true;
        }
        return false;
    }

inline bool option_is_selected(dom_node& option)
    {
        if (option.form_control().selectedness_initialized) {
            return option.form_control().selectedness;
        }
        auto* select = containing_select(option);
        if (select == nullptr) return option.attributes.contains("selected");
        if (select->form_control().selection_explicitly_empty) return false;
        std::vector<dom_node*> options;
        collect_descendants_by_tag(*select, "option", options);
        const auto has_live_selection = std::any_of(
            options.begin(), options.end(), [](const auto* candidate) {
                return candidate != nullptr
                    && candidate->form_control().selectedness_initialized
                    && candidate->form_control().selectedness;
            });
        if (has_live_selection) return false;
        const auto authored = std::find_if(
            options.begin(),
            options.end(),
            [](const auto* candidate) {
                return candidate != nullptr && candidate->attributes.contains("selected");
            });
        if (authored != options.end()) {
            return select->attributes.contains("multiple")
                ? option.attributes.contains("selected")
                : *authored == &option;
        }
        if (select->attributes.contains("multiple")) return false;
        return !options.empty() && options.front() == &option;
    }

inline std::string option_text(const dom_node& option)
    {
        std::string text;
        const auto append=[&](auto&& self,const dom_node& node)->void {
            if(node.kind==dom_node_kind::text) text+=node.text_content;
            for(auto* child:node.children) if(child) self(self,*child);
        };
        append(append,option);
        std::string result;bool space=false;
        for(char c:text) {
            if(c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\f') {space=!result.empty();continue;}
            if(space) result+=' ';
            result+=c;space=false;
        }
        return result;
    }

inline std::string option_label(const dom_node& option)
    {
        if (auto authored = option.attributes.find("label");
            authored != option.attributes.end() && !authored->second.empty()) {
            return authored->second;
        }
        return option_text(option);
    }

inline std::string option_value(const dom_node& option) {
    if(auto authored=option.attributes.find("value");authored!=option.attributes.end()) return authored->second;
    return option_text(option);
}

inline std::vector<dom_node*> select_options(dom_node& select)
    {
        std::vector<dom_node*> options;
        collect_descendants_by_tag(select,"option",options);
        return options;
    }

inline dom_node* selected_option(dom_node& select)
    {
        auto options=select_options(select);
        const auto selected=std::find_if(options.begin(),options.end(),[](auto* option){
            return option!=nullptr && option_is_selected(*option);
        });
        return selected==options.end()?nullptr:*selected;
    }

inline bool select_option(dom_node& select,dom_node& option)
    {
        if(containing_select(option)!=&select || option_is_disabled(option)) return false;
        auto options=select_options(select);
        if(std::find(options.begin(),options.end(),&option)==options.end()) return false;
        const auto* previous=selected_option(select);
        select.mutable_form_control().selection_explicitly_empty=false;
        for(auto* candidate:options) {
            auto& state=candidate->mutable_form_control();
            state.selectedness_initialized=true;
            state.selectedness=candidate==&option;
        }
        return previous!=&option;
    }

inline void set_select_value(dom_node& select,std::string_view value) {
    std::vector<dom_node*> options;collect_descendants_by_tag(select,"option",options);
    bool matched=false;
    for(auto* option:options) {
        auto& state=option->mutable_form_control();
        state.selectedness_initialized=true;
        state.selectedness=!matched && option_value(*option)==value;
        matched=matched || state.selectedness;
    }
    select.mutable_form_control().selection_explicitly_empty=!matched;
}

inline bool is_text_control(const dom_node* node)
    {
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")
            || node->attributes.contains("disabled")) {
            return false;
        }
        if (node->tag == "textarea") return true;
        const auto type = node->attributes.find("type");
        if (type == node->attributes.end()) return true;
        return type->second != "hidden" && type->second != "checkbox" && type->second != "radio"
            && type->second != "button" && type->second != "submit"
            && type->second != "reset" && type->second != "file"
            && type->second != "image" && type->second != "range"
            && type->second != "color";
    }

// Initialize the live value once from authored markup. No HTML parser is needed.
inline void ensure_text_value(dom_node& node) {
    auto& control=node.mutable_form_control();
    if(control.value_initialized) return;
    if(node.tag=="textarea") {
        control.value.clear();
        std::string text;
        const auto collect=[&](auto&& self,const dom_node& current)->void {
            if(current.kind==dom_node_kind::text) text+=current.text_content;
            for(auto* child:current.children) if(child) self(self,*child);
        };
        collect(collect,node);
        for(size_t i=0;i<text.size();++i) {
            if(text[i]=='\r') {control.value+='\n';if(i+1<text.size() && text[i+1]=='\n') ++i;}
            else control.value+=text[i];
        }
        control.dirty_value=false;
    } else {
        auto attribute=node.attributes.find("value");
        control.value=attribute==node.attributes.end()?std::string{}:attribute->second;
    }
    control.value_initialized=true;
    control.selection_start=control.selection_end=control.value.size();
    control.selection_direction=text_selection_direction::none;
}

inline size_t previous_utf8_boundary(const std::string& value,size_t index) {
    index=std::min(index,value.size());
    if(!index) return 0;
    --index;
    while(index && (static_cast<unsigned char>(value[index])&0xc0U)==0x80U) --index;
    return index;
}
inline size_t next_utf8_boundary(const std::string& value,size_t index) {
    if(index>=value.size()) return value.size();
    ++index;
    while(index<value.size() && (static_cast<unsigned char>(value[index])&0xc0U)==0x80U) ++index;
    return index;
}

}
