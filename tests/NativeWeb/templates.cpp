#include "native_web_templates_ui.hpp"
#include <stdexcept>
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    webscene::native_web::document d;
    const auto view = compiled_ui::build(d);
    auto parent = view.named("items");
    check(!d.find("item"), "template must stay inert");
    auto first = compiled_ui::instantiate(d, parent, "item");
    auto second = compiled_ui::instantiate(d, parent, "item");
    check(first.roots.size() == 1 && second.roots.size() == 1, "template roots");
    check(first.named("label") != second.named("label"), "instance-local references");
    d.set_text(first.named("label"), "First");
    d.attribute(second.named("row"), "class", "row selected");
    d.render(500, 400);
    check(d.bounds(first.named("row")).height == 30, "first instance style");
    check(d.bounds(second.named("row")).height == 45, "second instance live style");
    int calls = 0;
    auto sub = d.on(first.named("remove"), "click", [&](auto&) {
        ++calls;
        for (auto root : first.roots) d.remove(root);
    });
    d.dispatch(first.named("remove"), "click");
    check(calls == 1, "native template event");
    d.render(500, 400);
    check(d.bounds(second.named("row")).height == 45, "independent instance survives removal");
    auto mixed_parent = d.element(d.body(), "div");
    auto mixed = compiled_ui::instantiate(d, mixed_parent, "mixed");
    check(mixed.roots.size() == 3, "mixed template tracks whitespace, element and text roots");
    check(d.text_content(mixed_parent) == " Value tail ", "mixed template preserves original text");
    for (auto root : mixed.roots) d.remove(root);
    check(d.text_content(mixed_parent).empty(), "template removal leaves no orphan text");
    bool missing = false;
    try { compiled_ui::instantiate(d, parent, "unknown"); }
    catch (const std::invalid_argument&) { missing = true; }
    check(missing, "unknown template rejected");
}
