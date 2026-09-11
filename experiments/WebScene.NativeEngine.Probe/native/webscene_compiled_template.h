#pragma once
#include "webscene_native_dom.h"
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace webscene_native {
struct compiled_template_argument {
    std::string text;
    dom_node* node{};
};
using compiled_template_arguments = std::span<const compiled_template_argument>;
using compiled_dom_factory = std::function<dom_node&(native_document&, compiled_template_arguments)>;
using compiled_dom_factories = std::unordered_map<std::string, compiled_dom_factory>;
inline void compiled_attribute(dom_node& node, const std::string& name, std::string value) {
    if (name == "id") node.id_attribute = value;
    if (name == "class") node.class_name = value;
    node.attributes[name] = std::move(value);
}
inline const compiled_template_argument& compiled_argument(compiled_template_arguments args, size_t index) {
    if (index >= args.size()) throw std::invalid_argument("Missing compiled template argument");
    return args[index];
}
inline const std::string& compiled_argument_text(compiled_template_arguments args, size_t index) {
    const auto& argument = compiled_argument(args, index);
    if (argument.node) throw std::invalid_argument("A DOM node cannot be used as a text attribute");
    return argument.text;
}
inline void compiled_append_argument(native_document& document, dom_node& parent,
    const compiled_template_argument& value) {
    if (value.node) {
        if (document.find_by_native_id(value.node->id) != value.node)
            throw std::invalid_argument("Template argument belongs to another document");
        if (value.node->parent) throw std::invalid_argument("Template node argument must be detached");
        if (value.node->tag == "#document-fragment") {
            auto children = value.node->children;
            for (auto* child : children) {
                if (!document.parser_remove_from_parent(*child) || !document.append_child(parent, *child))
                    throw std::logic_error("Unable to attach compiled template child");
            }
        } else if (!document.append_child(parent, *value.node))
            throw std::logic_error("Unable to attach compiled template node");
    } else if (!value.text.empty()) {
        auto& text = document.create_node(dom_node_kind::text, "#text");
        text.text_content = value.text;
        document.append_child(parent, text);
    }
}
}
