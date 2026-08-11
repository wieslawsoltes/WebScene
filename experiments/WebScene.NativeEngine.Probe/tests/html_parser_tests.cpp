#include "webscene_html_parser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace webscene_native;

namespace {

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "HTML parser test failed: " << message << '\n';
    std::exit(1);
}

dom_node* first(dom_node& parent, std::string_view tag)
{
    for (auto* child : parent.children) {
        if (child != nullptr && child->tag == tag) return child;
    }
    return nullptr;
}

dom_node* descendant(dom_node& parent, std::string_view tag)
{
    if (parent.tag == tag) return &parent;
    for (auto* child : parent.children) {
        if (child == nullptr) continue;
        if (auto* match = descendant(*child, tag); match != nullptr) return match;
    }
    return nullptr;
}

size_t count_kind(const dom_node& parent, dom_node_kind kind)
{
    size_t count = parent.kind == kind ? 1U : 0U;
    for (const auto* child : parent.children) {
        if (child != nullptr) count += count_kind(*child, kind);
    }
    if (parent.template_contents != nullptr) {
        count += count_kind(*parent.template_contents, kind);
    }
    return count;
}

void document_tree_construction()
{
    native_document document;
    auto& root = document.create_node(dom_node_kind::internal, "#document-root");
    const auto result = parse_html_document(
        document,
        root,
        "<!doctype html><!--marker--><title>x</title><p><b>one<i>two</b>three</i></p>"
        "<table>outside<tr><td>cell</table><svg viewBox='0 0 1 1'><path/></svg>");
    require(static_cast<bool>(result), result.error.c_str());
    require(result.element_count >= 10, "expected html5ever-created elements");
    require(result.comment_count == 1, "comments must be preserved");
    require(result.doctype_count == 1, "doctype must be preserved");
    require(result.quirks_mode == html_quirks_mode::no_quirks, "HTML doctype must select no-quirks mode");
    require(first(root, "#doctype") != nullptr, "doctype must be attached to document root");
    auto* html = first(root, "html");
    require(html != nullptr, "document must contain an html element");
    require(first(*html, "head") != nullptr, "document must contain an inferred head");
    auto* body = first(*html, "body");
    require(body != nullptr, "document must contain an inferred body");
    auto* table = first(*body, "table");
    require(table != nullptr, "document must contain the authored table");
    const auto table_position = std::find(
        body->children.begin(), body->children.end(), table);
    require(
        table_position != body->children.begin() && table_position != body->children.end(),
        "foster-parented table text must precede the table");
    const auto* fostered_text = *(table_position - 1);
    require(
        fostered_text != nullptr
            && fostered_text->kind == dom_node_kind::text
            && fostered_text->text_content == "outside",
        "foster-parented table text must be immediately before the table");
    auto* svg = descendant(*body, "svg");
    require(svg != nullptr, "SVG subtree must be retained");
    require(svg->namespace_uri() == "http://www.w3.org/2000/svg", "SVG namespace must be explicit");
    require(descendant(*body, "tbody") != nullptr, "table insertion modes must infer tbody");
}

void context_fragments_and_templates()
{
    native_document document;
    auto& table = document.create_node(dom_node_kind::element, "table");
    const auto table_result = parse_html_fragment(
        document, table, "<tr><td>A<td>B", "table");
    require(static_cast<bool>(table_result), table_result.error.c_str());
    require(descendant(table, "tbody") != nullptr, "table fragment context must infer tbody");
    require(descendant(table, "td") != nullptr, "table fragment context must create cells");

    auto& output = document.create_node(dom_node_kind::document_fragment, "#fragment");
    const auto template_result = parse_html_fragment(
        document, output, "<template><span>inside</span></template>", "body");
    require(static_cast<bool>(template_result), template_result.error.c_str());
    auto* element = descendant(output, "template");
    require(element != nullptr, "template element must be created");
    require(element->template_contents != nullptr, "template contents must use a separate fragment");
    require(descendant(*element->template_contents, "span") != nullptr, "template content tree must be populated");
}

void diagnostic_comment_policy()
{
    native_document document;
    auto& root = document.create_node(dom_node_kind::document_fragment, "#fragment");
    html_parse_options options;
    options.preserve_comments = false;
    const auto result = parse_html_fragment(
        document, root, "before<!--discarded-->after", "body",
        "http://www.w3.org/1999/xhtml", options);
    require(static_cast<bool>(result), result.error.c_str());
    require(result.comment_count == 0, "diagnostic mode must report no retained comments");
    require(count_kind(root, dom_node_kind::comment) == 0, "diagnostic mode must discard comment nodes");
    require(count_kind(root, dom_node_kind::text) == 1, "adjacent text must be coalesced across a discarded comment");
}

} // namespace

int main()
{
    document_tree_construction();
    context_fragments_and_templates();
    diagnostic_comment_policy();
    std::cout << "html5ever tree-sink tests passed\n";
    return 0;
}
