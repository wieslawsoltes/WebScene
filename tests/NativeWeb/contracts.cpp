#include "native_web_contracts_ui.hpp"
#include <atomic>
#include <webscene/compiled_variables.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
static void check(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}
int main() {
  using namespace webscene::native_web;
  {
    using expression = variable_expression;
    auto token = [](std::string value) { return expression{expression::kind::token, value}; };
    auto ref = [](std::string name) { return expression{expression::kind::reference, name}; };
    specified_variables variables;
    variables["--accent"] = {token("#5ac6d2")};
    variables["--border"] = {token("1px"), token("solid"), ref("--accent")};
    auto parent = compute_variables(variables);
    check(parent.at("--border") == variable_tokens({"1px", "solid", "#5ac6d2"}), "compiled variable substitution");
    auto child = compute_variables({{"--accent", {token("#157a8b")}}}, parent);
    check(child.at("--border") == parent.at("--border"), "inherited variable references stay computed");
    expression fallback{expression::kind::reference, "--missing", {token("fallback")}, true};
    variables["--fallback"] = {fallback};
    variables["--a"] = {ref("--b")};
    variables["--b"] = {ref("--a")};
    variables["--rescue"] = {{expression::kind::reference, "--a", {token("safe")}, true}};
    variables["--hidden-cycle"] = {{expression::kind::reference, "--accent", {ref("--hidden-cycle")}, true}};
    auto result = compute_variables(variables);
    check(!result.at("--a") && !result.at("--b"), "cycles invalidate variables");
    check(!result.at("--hidden-cycle"), "unused fallback participates in cycles");
    check(result.at("--rescue") == variable_tokens({"safe"}), "invalid variable uses fallback");
    check(result.at("--fallback") == variable_tokens({"fallback"}), "missing variable uses fallback");
  }
  {
    document themed;
    auto panel = themed.element(themed.body(), "div");
    auto root_rule = [](const char *value, bool light) {
      rule r;
      selector_part part;
      part.root = true;
      if (light) part.attributes.push_back({"data-theme", "light", true});
      r.match.parts.push_back(part);
      r.match.specificity = light ? 20 : 10;
      r.declarations.push_back({false, nullptr, "--panel-width",
          {{variable_expression::kind::token, value}}});
      return r;
    };
    themed.add_rule(root_rule("wide", false));
    themed.add_rule(root_rule("narrow", true));
    rule consumer;
    selector_part panel_selector;
    panel_selector.tag = "div";
    consumer.match.parts.push_back(panel_selector);
    consumer.declarations.push_back({false, +[](style &s) {
      auto value = s.variable("--panel-width");
      if (value && *value)
        s.set_width({**value == variable_tokens{"wide"} ? 222.f : 195.f, length_unit::pixels});
    }});
    themed.add_rule(std::move(consumer));
    themed.render(800, 600);
    check(themed.bounds(panel).width == 222, "cascade inherits compiled variable");
    themed.attribute(themed.root(), "data-theme", "light");
    themed.render(800, 600);
    check(themed.bounds(panel).width == 195, "theme selector recomputes inherited variables");
    auto important = root_rule("wide", false);
    important.declarations.front().important = true;
    themed.add_rule(std::move(important));
    themed.render(800, 600);
    check(themed.bounds(panel).width == 222, "important variable beats theme specificity");
  }
  {
    document layered;
    auto canvas = layered.element(layered.body(), "canvas");
    auto overlay = layered.element(layered.body(), "button");
    layered.set_text(overlay, "Overlay");
    layered.attribute(overlay, "data-action", "view-top");
    auto icon = layered.element(overlay, "span");
    check(layered.parent(icon) == overlay, "native parent supports delegated icon actions");
    check(layered.attribute(layered.parent(icon), "data-action") == "view-top",
          "native action attribute remains available without parsing HTML");
    check(!layered.attribute(icon, "data-action"), "missing attribute differs from an empty value");
    layered.set_external_canvas(canvas, true);
    const auto &scene = layered.render(800, 600);
    bool placed = false, text_after_canvas = false;
    for (const auto &command : scene.commands) {
      if (command.kind == 257 && command.node_id == canvas) placed = true;
      if (placed && command.kind == 3) text_after_canvas = true;
    }
    check(placed && text_after_canvas, "external canvas placement precedes later DOM text");
    layered.set_external_canvas(canvas, false);
    const auto &cleared = layered.render(800, 600);
    for (const auto &command : cleared.commands)
      check(command.kind != 257 || command.node_id != canvas,
            "detached external canvas removes its placement");
  }
  {
    auto difference = add_compiled_lengths({50,length_unit::percent}, {32,length_unit::pixels}, true);
    check(difference && difference->value == 50 && difference->pixel_offset == -32 &&
          difference->unit == length_unit::percent, "compiled length subtraction retains percentage basis");
    auto reverse = add_compiled_lengths({32,length_unit::pixels}, {50,length_unit::percent}, true);
    check(reverse && reverse->value == -50 && reverse->pixel_offset == 32,
          "compiled subtraction preserves operand order");
    check(!add_compiled_lengths({1,length_unit::em}, {1,length_unit::percent}),
          "unrepresentable mixed units are not silently flattened");
    check(!add_compiled_lengths({}, {1,length_unit::pixels}),
          "automatic length is not an arithmetic operand");
  }
  document d;
  auto refs = compiled_ui::build(d);
  d.render(800, 600);
  check(d.bounds(d.find("after-break")).y > d.bounds(d.find("before-break")).y,
        "compiled br moves following inline content to a new line");
  d.remove(d.find("explicit-break"));
  d.render(800, 600);
  check(std::abs(d.bounds(d.find("after-break")).y - d.bounds(d.find("before-break")).y) < 0.01f,
        "removing compiled br restores a shared inline line");
  auto padding_longhand = d.find("padding-longhand");
  check(d.bounds(padding_longhand).width == 30 && d.bounds(padding_longhand).height == 16,
        "variable longhand fallback overrides one shorthand side");
  d.attribute(padding_longhand, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(padding_longhand).width == 23 && d.bounds(padding_longhand).height == 16,
        "invalid padding longhand resets only its side without restoring earlier declaration");
  d.remove_attribute(padding_longhand, "class");
  d.render(800, 600);
  check(d.bounds(padding_longhand).width == 30, "padding longhand fallback recovers");
  auto variable_gap = d.find("variable-gap");
  auto gap_first = d.find("gap-first"), gap_second = d.find("gap-second"), gap_third = d.find("gap-third");
  check(d.bounds(gap_second).x - d.bounds(gap_first).x == 17 &&
        d.bounds(gap_third).y - d.bounds(gap_first).y == 13, "variable gap expands distinct row and column values");
  d.attribute(variable_gap, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(gap_second).x - d.bounds(gap_first).x == 10 &&
        d.bounds(gap_third).y - d.bounds(gap_first).y == 10, "invalid variable gap resets both axes");
  d.remove_attribute(variable_gap, "class");
  d.render(800, 600);
  d.attribute(variable_gap, "class", "longhand");
  d.render(800, 600);
  check(d.bounds(gap_second).x - d.bounds(gap_first).x == 21 &&
        d.bounds(gap_third).y - d.bounds(gap_first).y == 13,
        "variable column-gap fallback overrides shorthand without changing row gap");
  for (const auto* invalid_class : {"longhand multi", "longhand negative"}) {
    d.attribute(variable_gap, "class", invalid_class);
    d.render(800, 600);
    check(d.bounds(gap_second).x - d.bounds(gap_first).x == 10 &&
          d.bounds(gap_third).y - d.bounds(gap_first).y == 13,
          "invalid gap longhand resets only its axis and does not use var fallback");
  }
  d.remove_attribute(variable_gap, "class");
  d.render(800, 600);
  check(d.bounds(gap_second).x - d.bounds(gap_first).x == 17,
        "removing gap longhand restores shorthand");
  auto variable_margin = d.find("variable-margin"), margin_container = d.find("margin-container");
  check(d.bounds(variable_margin).x - d.bounds(margin_container).x == 40, "variable auto margins center a block");
  d.attribute(variable_margin, "class", "shift");
  d.render(800, 600);
  check(d.bounds(variable_margin).x - d.bounds(margin_container).x == -5, "signed variable margin clears auto flags");
  d.attribute(variable_margin, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(variable_margin).x == d.bounds(margin_container).x, "invalid variable margin resets to zero");
  d.attribute(variable_margin, "class", "longhand");
  d.render(800, 600);
  check(d.bounds(variable_margin).x - d.bounds(margin_container).x == 10,
        "margin longhand replaces one auto shorthand side");
  d.attribute(variable_margin, "class", "longhand bad-longhand");
  d.render(800, 600);
  check(d.bounds(variable_margin).x == d.bounds(margin_container).x,
        "invalid margin longhand resets its side instead of taking fallback");
  d.attribute(variable_margin, "class", "longhand auto-longhand");
  d.render(800, 600);
  check(d.bounds(variable_margin).x - d.bounds(margin_container).x == 40,
        "variable longhand restores auto centering");
  d.remove_attribute(variable_margin, "class");
  d.render(800, 600);
  check(d.bounds(variable_margin).x - d.bounds(margin_container).x == 40,
        "removing margin longhand preserves shorthand auto state");
  auto translated = d.find("translated"), translate_parent = d.find("translate-parent");
  check(d.bounds(translated).x - d.bounds(translate_parent).x == 10, "compiled percentage translation uses own width");
  d.render(800, 10000);
  auto clip_parent = d.find("translation-clip"), clip_child = d.find("translation-clipped");
  const auto &clip_scene = d.render(800, 10000);
  size_t clip_begin = clip_scene.commands.size(), child_paint = clip_begin, clip_end = clip_begin;
  for (size_t i = 0; i < clip_scene.commands.size(); ++i) {
    const auto &command = clip_scene.commands[i];
    if (command.node_id == clip_parent && command.kind == 12) {
      clip_begin = i;
      check(command.width == 20 && command.height == 10, "overflow clip uses ancestor viewport");
    }
    if (command.node_id == clip_child && command.rgba == 0x123456ffu) child_paint = i;
    if (command.node_id == clip_parent && command.kind == 13) clip_end = i;
  }
  check(clip_begin < child_paint && child_paint < clip_end && clip_end < clip_scene.commands.size(),
        "native scene brackets translated descendant paint with overflow clip");
  int clipped_hits = 0;
  auto clip_subscription = d.on(clip_child, "pointerdown", [&](event&) { ++clipped_hits; });
  const auto clip_area = d.bounds(clip_parent);
  const auto click_clip = [&](float offset) {
    d.pointer("pointerdown", clip_area.x + offset, clip_area.y + 1);
    d.pointer("pointerup", clip_area.x + offset, clip_area.y + 1);
  };
  click_clip(15);
  check(clipped_hits == 1, "visible translated portion receives input");
  click_clip(25);
  check(clipped_hits == 1, "overflow hidden excludes translated portion outside ancestor");
  d.attribute(clip_parent, "class", "visible");
  d.render(800, 10000);
  click_clip(25);
  check(clipped_hits == 2, "overflow visible restores translated outside hit region");
  check(std::none_of(clip_scene.commands.begin(), clip_scene.commands.end(), [&](const auto& command) {
    return command.node_id == clip_parent && (command.kind == 12 || command.kind == 13);
  }), "overflow visible removes ancestor clip commands");
  d.attribute(clip_parent, "class", "empty");
  d.render(800, 10000);
  check(std::any_of(clip_scene.commands.begin(), clip_scene.commands.end(), [&](const auto& command) {
    return command.node_id == clip_parent && command.kind == 12 && command.height == 0;
  }), "zero-height overflow viewport retains empty scene clip");
  auto empty_area = d.bounds(clip_parent);
  d.pointer("pointerdown", empty_area.x + 15, empty_area.y + 1);
  d.pointer("pointerup", empty_area.x + 15, empty_area.y + 1);
  check(clipped_hits == 2, "zero-height clipped viewport excludes retained descendant input");
  check(d.bounds(clip_child).height == 10, "empty clip retains descendant geometry");
  d.remove_attribute(clip_parent, "class");
  d.render(800, 10000);
  click_clip(15);
  check(clipped_hits == 3, "restoring clip height restores descendant targeting");
  d.attribute(clip_parent, "class", "mixed");
  d.render(800, 10000);
  click_clip(25);
  check(clipped_hits == 3, "visible overflow computes to auto beside hidden axis");
  check(std::any_of(clip_scene.commands.begin(), clip_scene.commands.end(), [&](const auto& command) {
    return command.node_id == clip_parent && command.kind == 12;
  }), "mixed visible-hidden overflow retains scene clip");
  int translated_hits = 0;
  auto translated_subscription = d.on(translated, "pointerdown", [&](event&) { ++translated_hits; });
  auto translated_area = d.bounds(translated);
  d.pointer("pointerdown", translated_area.x + translated_area.width - 1, translated_area.y + 1);
  d.pointer("pointerup", translated_area.x + translated_area.width - 1, translated_area.y + 1);
  check(translated_hits == 1, "translated element receives pointer at displaced right edge");
  d.pointer("pointerdown", d.bounds(translate_parent).x + 1, translated_area.y + 1);
  d.pointer("pointerup", d.bounds(translate_parent).x + 1, translated_area.y + 1);
  check(translated_hits == 1, "translation removes hit coverage from original left edge");
  auto translated_child = d.find("translated-child");
  check(d.bounds(translated_child).x == d.bounds(translated).x + 2,
        "nested translation composes child and parent offsets");
  int child_hits = 0;
  auto child_subscription = d.on(translated_child, "pointerdown", [&](event& e) { ++child_hits; e.stop_propagation(); });
  auto child_area = d.bounds(translated_child);
  d.pointer("pointerdown", child_area.x + 1, child_area.y + 1);
  d.pointer("pointerup", child_area.x + 1, child_area.y + 1);
  check(child_hits == 1, "nested translated child receives pointer input");
  const auto sibling_flow_offset = d.bounds(d.find("translate-sibling")).y - d.bounds(translate_parent).y;
  check(sibling_flow_offset == 10, "untranslated sibling follows source box height");
  d.attribute(translated, "class", "both");
  d.render(800, 10000);
  check(d.bounds(translated).x - d.bounds(translate_parent).x == 10 &&
        d.bounds(translated).y - d.bounds(translate_parent).y == 5,
        "two-axis percentage translate uses own width and height");
  check(d.bounds(d.find("translate-sibling")).y - d.bounds(translate_parent).y == sibling_flow_offset,
        "vertical transform does not move following sibling flow position");
  check(d.bounds(translated_child).x == d.bounds(translated).x + 2 &&
        d.bounds(translated_child).y == d.bounds(translated).y,
        "descendant follows parent vertical translation");
  d.render(1600, 10000);
  check(d.bounds(translated).width == 40 &&
        d.bounds(translated).x - d.bounds(translate_parent).x == 20 &&
        d.bounds(translated).y - d.bounds(translate_parent).y == 5,
        "percentage translation re-resolves after viewport-driven element resize");
  translated_area = d.bounds(translated);
  d.pointer("pointerdown", translated_area.x + translated_area.width - 1, translated_area.y + 1);
  d.pointer("pointerup", translated_area.x + translated_area.width - 1, translated_area.y + 1);
  check(translated_hits == 2, "translated hit geometry follows resized element");
  d.render(800, 10000);
  check(d.bounds(translated).width == 20 && d.bounds(translated).x - d.bounds(translate_parent).x == 10,
        "percentage translation returns without accumulated offsets");
  d.attribute(translated, "class", "reset");
  d.render(800, 600);
  check(d.bounds(translated).x == d.bounds(translate_parent).x, "transform none resets translation");
  check(d.bounds(d.find("translate-sibling")).y - d.bounds(translate_parent).y == sibling_flow_offset,
        "clearing transform preserves following sibling flow position");
  auto scroll_container = d.find("native-scroll"), scroll_content = d.find("scroll-content");
  d.scroll_to(scroll_container, 0, 15);
  d.render(800, 600);
  check(d.scroll_offset(scroll_container).second == 15 &&
        d.bounds(scroll_content).y == d.bounds(scroll_container).y - 15,
        "native scroll API moves compiled content");
  d.scroll_to(scroll_container, 0, 1000);
  d.render(800, 600);
  check(d.scroll_offset(scroll_container).second == 40, "native scroll clamps to content extent");
  d.attribute(scroll_content, "class", "short");
  d.render(800, 600);
  check(d.scroll_offset(scroll_container).second == 10 &&
        d.bounds(scroll_content).y == d.bounds(scroll_container).y - 10,
        "content shrink clamps retained scroll and geometry in same render");
  d.attribute(scroll_container, "class", "expanded");
  d.render(800, 600);
  check(d.scroll_offset(scroll_container).second == 0 &&
        d.bounds(scroll_content).y == d.bounds(scroll_container).y,
        "viewport expansion clears scroll when content fits");
  d.remove_attribute(scroll_content, "class");
  d.remove_attribute(scroll_container, "class");
  d.render(800, 600);
  d.scroll_to(scroll_container, 0, -10);
  d.render(800, 600);
  check(d.scroll_offset(scroll_container).second == 0, "native scroll clamps negative offset");
  d.render(800, 10000);
  auto scroll_area = d.bounds(scroll_container);
  d.wheel(scroll_area.x + 1, scroll_area.y + 1, 12);
  d.render(800, 10000);
  check(d.scroll_offset(scroll_container).second == 12, "wheel defaults to nearest native scroll container");
  {
    auto cancel_wheel = d.on(scroll_content, "wheel", [](event& e) { e.prevent_default(); });
    d.wheel(scroll_area.x + 1, scroll_area.y + 1, 9);
    check(d.scroll_offset(scroll_container).second == 12, "preventDefault cancels native wheel scrolling");
  }
  d.wheel(scroll_area.x + 1, scroll_area.y + 1, -7);
  check(d.scroll_offset(scroll_container).second == 5, "wheel resumes after listener disposal");
  d.render(800, 10000);
  d.attribute(scroll_container, "class", "hidden-mode");
  d.render(800, 10000);
  d.scroll_to(scroll_container, 0, 17);
  check(d.scroll_offset(scroll_container).second == 17, "overflow hidden permits programmatic scrolling");
  d.render(800, 10000);
  d.wheel(scroll_area.x + 1, scroll_area.y + 1, 6);
  check(d.scroll_offset(scroll_container).second == 17, "overflow hidden suppresses wheel default");
  d.attribute(scroll_container, "class", "clip-mode");
  d.render(800, 10000);
  check(d.scroll_offset(scroll_container).second == 0 &&
        d.bounds(scroll_content).y == d.bounds(scroll_container).y,
        "switching to overflow clip clears retained offset during layout");
  d.scroll_to(scroll_container, 0, 17);
  check(d.scroll_offset(scroll_container).second == 0, "overflow clip rejects programmatic scrolling");
  d.remove_attribute(scroll_container, "class");
  d.render(800, 10000);
  d.scroll_to(scroll_container, 0, 10);
  d.attribute(scroll_container, "class", "visible-mode");
  d.render(800, 10000);
  check(d.scroll_offset(scroll_container).second == 0 && d.bounds(scroll_content).y == d.bounds(scroll_container).y,
        "switching to overflow visible clears retained scrolling");
  d.remove_attribute(scroll_container, "class");
  d.render(800, 10000);
  int removal_calls = 0;
  auto remove_during_wheel = d.on(scroll_content, "wheel", [&](event&) {
    ++removal_calls;
    d.remove(scroll_container);
  });
  d.wheel(scroll_area.x + 1, scroll_area.y + 1, 6);
  check(removal_calls == 1 && !d.find("native-scroll"),
        "wheel default safely skips scroll subtree removed by handler");

  d.render(800, 600);
  d.render(800, 10000);
  auto scroll_outer = d.find("scroll-outer"), scroll_inner = d.find("scroll-inner");
  auto nested_area = d.bounds(scroll_inner);
  d.wheel(nested_area.x + 1, nested_area.y + 1, 5);
  check(d.scroll_offset(scroll_inner).second == 5 && d.scroll_offset(scroll_outer).second == 0,
        "nested wheel prefers inner scroll container");
  d.scroll_to(scroll_inner, 0, 20);
  d.render(800, 10000);
  d.wheel(nested_area.x + 1, nested_area.y + 1, 7);
  check(d.scroll_offset(scroll_inner).second == 20 && d.scroll_offset(scroll_outer).second == 7,
        "wheel at inner boundary reaches scrollable ancestor");
  d.render(800, 10000);
  nested_area = d.bounds(scroll_outer);
  d.wheel(nested_area.x + 1, nested_area.y + 1, -3);
  check(d.scroll_offset(scroll_inner).second == 17 && d.scroll_offset(scroll_outer).second == 7,
        "reverse wheel returns to inner container when it can scroll");
  d.render(800, 600);
  auto align_variable = d.find("align-variable"), align_item = d.find("align-item");
  check(d.bounds(align_item).x - d.bounds(align_variable).x == 45, "variable text-align fallback centers inline item");
  d.attribute(align_variable, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(align_item).x - d.bounds(align_variable).x == 90, "invalid text-align variable restores inherited alignment");
  auto transform_text = d.find("transform-text");
  const auto &text_scene = d.render(800, 10000);
  const auto contains_painted_text = [&](std::string_view expected) {
    return std::string_view(text_scene.bytes.data(), text_scene.bytes.size()).find(expected) != std::string_view::npos;
  };
  check(contains_painted_text("CASEAUDITPROBE"), "variable text transform reaches native paint text");
  check(d.text_content(transform_text) == "CaseAuditProbe", "text transform preserves DOM source text");
  d.attribute(transform_text, "class", "invalid");
  d.render(800, 10000);
  check(contains_painted_text("caseauditprobe") && !contains_painted_text("CASEAUDITPROBE"),
        "invalid variable text transform inherits parent lowercase");
  d.remove_attribute(transform_text, "class");
  d.render(800, 10000);
  check(contains_painted_text("CASEAUDITPROBE"), "variable text transform fallback recovers after mutation");
  d.render(800, 600);
  auto whitespace_variable = d.find("whitespace-variable");
  check(d.bounds(whitespace_variable).height == 40, "preformatted variable newline creates two lines");
  d.attribute(whitespace_variable, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 20, "invalid whitespace inherits normal collapse");
  d.remove_attribute(whitespace_variable, "class");
  d.set_text(whitespace_variable, "A\n\nB");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 60, "preformatted blank line contributes line height");
  d.set_text(whitespace_variable, "   ");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 20, "whitespace-only pre content creates a line box");
  d.set_text(whitespace_variable, "A\n");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 20, "trailing pre newline does not add an empty final line");
  d.set_text(whitespace_variable, "\nA");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 40, "leading pre newline retains its blank line");
  d.attribute(whitespace_variable, "class", "preline");
  d.set_text(whitespace_variable, "A   B\n\nC");
  const auto &preline_scene = d.render(800, 10000);
  check(d.bounds(whitespace_variable).height == 60, "pre-line preserves explicit and blank lines");
  check(std::string_view(preline_scene.bytes.data(), preline_scene.bytes.size()).find("A   B") == std::string_view::npos,
        "pre-line collapses repeated spaces in paint text");
  d.set_text(whitespace_variable, "MMMM MMMM\nX");
  d.attribute(whitespace_variable, "class", "preline narrow");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 60,
        "pre-line combines soft wrapping with explicit newline");
  d.attribute(whitespace_variable, "class", "preline");
  d.render(800, 600);
  check(d.bounds(whitespace_variable).height == 40,
        "wider pre-line removes soft wrap while retaining explicit newline");

  d.remove_attribute(whitespace_variable, "class");
  d.set_text(whitespace_variable, "  Spaced  ");
  const auto &spaces_scene = d.render(800, 10000);
  check(std::string_view(spaces_scene.bytes.data(), spaces_scene.bytes.size()).find("  Spaced  ") != std::string_view::npos,
        "preformatted leading and trailing spaces reach scene text");
  d.set_text(whitespace_variable, "A\nB");
  d.render(800, 600);

  auto variable_padding = d.find("variable-padding");
  check(d.bounds(variable_padding).width == 28 && d.bounds(variable_padding).height == 14,
        "compiled variable padding expands both axes");
  d.attribute(variable_padding, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(variable_padding).width == 20 && d.bounds(variable_padding).height == 10,
        "invalid variable padding resets every side to initial zero");
  d.remove_attribute(variable_padding, "class");
  d.render(800, 600);
  check(d.bounds(variable_padding).width == 28, "variable padding recovers after mutation");
  auto hidden_probe = d.find("hidden-probe");
  check(d.bounds(hidden_probe).height == 0, "compiled hidden attribute suppresses layout");
  d.remove_attribute(hidden_probe, "hidden");
  d.render(800, 600);
  check(d.bounds(hidden_probe).height == 17, "native removal reveals compiled hidden element");
  d.attribute(hidden_probe, "hidden", "");
  d.render(800, 600);
  check(d.bounds(hidden_probe).height == 0, "native attribute update hides element again");
  check(d.text_content(d.find("inline-whitespace")) == "A B",
        "compiled HTML preserves whitespace between inline elements");
  check(d.text_content(d.find("preserved-whitespace")) == "  ",
        "compiled HTML retains whitespace-only preformatted content");
  rule compiled_variable_probe;
  selector_part probe_root;
  probe_root.root = true;
  compiled_variable_probe.match.parts.push_back(probe_root);
  compiled_variable_probe.declarations.push_back({false, +[](style &s) {
    check(s.color_with_opacity({{variable_expression::kind::reference,"--accent"}},.25f) == 0x5ac6d240u,
          "theme color mix preserves RGB and scales alpha");
    auto border = s.variable("--border");
    check(border && *border && (**border).size() == 3 &&
          (**border)[0].length && (**border)[0].length->value == 1 &&
          (**border)[1].text == "solid" && (**border)[2].color == 0x5ac6d2ffu,
          "compiler-generated variable expression reaches native cascade");
  }});
  d.add_rule(std::move(compiled_variable_probe));
  check(d.root() != d.body(), "HTML root differs from body");
  check(d.find("html-root") == d.root(),
        "compiled HTML root attributes preserved");
  auto target = refs.named("target"), other = refs.named("other");
  const auto &initial_scene = d.render(800, 600);
  check(d.bounds(d.body()).x == 3, "root style applies to HTML element");
  std::string scene_bytes(initial_scene.bytes.begin(),initial_scene.bytes.end());
  check(scene_bytes.find("fill=\"#5ac6d2\"") != std::string::npos,
        "compiled theme SVG fill reaches serialized scene");
  check(d.bounds(d.find("flex-min-height")).height == 400,
        "column flex basis uses the same constrained base as free-space accounting");
  check(d.bounds(d.find("flex-min-width")).width == 400,
        "row flex basis uses the same constrained base as free-space accounting");
  {
    bool middle_seen = false;
    auto handler = d.on(target, "pointerdown", [&](auto &event) {
      middle_seen = event.buttons == 4;
      event.prevent_default();
    });
    auto bounds = d.bounds(target);
    d.pointer("pointerdown", bounds.x + 1, bounds.y + 1, 4);
    check(middle_seen, "native pointer preserves middle-button mask");
    d.pointer("pointercancel", bounds.x + 1, bounds.y + 1, 0);
    unsigned clicks = 0;
    auto click = d.on(target, "click", [&](auto &) { ++clicks; });
    for (auto mask : {2u, 4u}) {
      d.pointer("pointerdown", bounds.x + 1, bounds.y + 1, mask);
      d.pointer("pointerup", bounds.x + 1, bounds.y + 1, 0);
    }
    check(clicks == 0, "auxiliary buttons do not synthesize primary click");
    d.pointer("pointerdown", bounds.x + 1, bounds.y + 1, 1);
    d.pointer("pointercancel", bounds.x + 1, bounds.y + 1, 0);
    d.pointer("pointerup", bounds.x + 1, bounds.y + 1, 0);
    check(clicks == 0, "cancelled primary press does not click");
    d.pointer("pointerdown", bounds.x + 1, bounds.y + 1, 1);
    d.pointer("pointerup", bounds.x + 1, bounds.y + 1, 0);
    check(clicks == 1, "primary button still activates after cancellation");
  }
  check(d.bounds(d.find("numeric-length")).width == 5.f &&
        d.bounds(d.find("numeric-length")).height == .75f,
        "compiled leading-decimal lengths preserve their numeric values");
  {
    auto parent = d.bounds(d.find("inset-parent"));
    auto child = d.bounds(d.find("inset-child"));
    check(child.x == parent.x + 40 && child.y == parent.y + 10 &&
          child.width == 140 && child.height == 60,
          "compiled four-sided inset constrains native position and size");
  }
  d.attribute(d.find("inset-child"), "data-invalid", "true");
  d.render(800, 600);
  check(d.bounds(d.find("inset-child")).width == 12 &&
        d.bounds(d.find("inset-child")).height == 13,
        "invalid inset variable preserves independently authored dimensions");
  check(d.bounds(d.find("inset-child")).x == d.bounds(d.find("inset-parent")).x &&
        d.bounds(d.find("inset-child")).y == d.bounds(d.find("inset-parent")).y,
        "invalid inset variable clears previous positional offsets");
  d.attribute(d.find("inset-child"), "data-invalid", "false");
  d.render(800, 600);
  check(std::string(initial_scene.bytes.begin(), initial_scene.bytes.end()).find("stroke-width=\".8\"") != std::string::npos,
        "compiled SVG stroke width reaches serialized geometry");
  check(std::string(initial_scene.bytes.begin(), initial_scene.bytes.end()).find("text-anchor=\"middle\"") != std::string::npos,
        "compiled SVG text anchor reaches serialized text");
  check(std::string(initial_scene.bytes.begin(), initial_scene.bytes.end()).find("antialiased") != std::string::npos,
        "compiled inherited font smoothing reaches native text scene");
  check(d.bounds(d.find("calc-child")).x == d.bounds(d.find("calc-parent")).x + 80 &&
        d.bounds(d.find("calc-child")).y == d.bounds(d.find("calc-parent")).y + 15,
        "compiled calc retains percentage and variable fallback arithmetic");
  d.render(1200, 600);
  check(d.bounds(d.find("calc-parent")).width == 300 &&
        d.bounds(d.find("calc-child")).x == d.bounds(d.find("calc-parent")).x + 130,
        "compiled calc re-resolves its percentage basis after viewport resize");
  d.render(800, 600);
  check(d.bounds(d.find("zero-variable")).width == 0,
        "unitless exponent zero retains length semantics after variable substitution");
  bool font_found = false;
  for (const auto &command : initial_scene.commands) {
    if (command.kind != 3 || command.flags >= initial_scene.strings.size()) continue;
    auto entry = initial_scene.strings[command.flags];
    std::string payload(initial_scene.bytes.data() + entry.byte_offset, entry.byte_length);
    if (payload.ends_with("\tFontProbe")) {
      check(payload.find("Consolas,monospace") != std::string::npos, "font shorthand family reaches scene");
      check(command.height == 19, "font shorthand line height reaches scene");
      font_found = true;
    }
  }
  check(font_found, "font shorthand text rendered");
  bool shadow_found = false;
  for (const auto &command : initial_scene.commands)
    if ((command.kind == 17 || command.kind == 18) && command.node_id == d.find("variable-probe"))
      shadow_found = true;
  check(shadow_found, "compiled variable shadow reaches scene");
  check(d.bounds(d.find("fraction-a")).width == 100, "compiled variable fractional first track");
  check(d.bounds(d.find("fraction-b")).width == 200, "compiled variable fractional second track");
  check(d.bounds(d.find("grid-left")).width == 222, "compiled grid variable resolves");
  check(d.bounds(d.find("grid-right")).width == 252, "compiled grid preserves fixed track");
  check(d.bounds(d.find("variable-probe")).width == 222, "compiled var width resolves");
  check(d.bounds(d.find("variable-probe")).height == 14, "compiled var fallback plus borders resolves");
  auto fraction_grid = d.find("fraction-grid");
  d.attribute(fraction_grid, "class", "reverse");
  d.render(800, 600);
  check(d.bounds(d.find("fraction-a")).width == 200, "fraction variable mutation reverses ratio");
  check(d.bounds(d.find("fraction-b")).width == 100, "fraction variable mutation second track");
  d.attribute(fraction_grid, "class", "invalid");
  d.render(800, 600);
  check(d.bounds(d.find("fraction-a")).width == 300, "invalid fraction resets whole grid declaration");
  check(d.bounds(d.find("fraction-b")).width == 300, "invalid fraction removes stale second track");
  d.remove_attribute(fraction_grid, "class");
  d.render(800, 600);
  check(d.bounds(d.find("fraction-a")).width == 100, "fraction fallback recovers after invalid value");
  check(d.bounds(d.find("fraction-b")).width == 200, "fraction fallback second track recovers");
  d.attribute(d.root(), "data-theme", "light");
  const auto &light_scene = d.render(800, 600);
  check(std::string(light_scene.bytes.begin(), light_scene.bytes.end())
                .find("fill=\"#112233\"") != std::string::npos,
        "native theme mutation updates compiled SVG paint");
  check(d.bounds(d.find("inset-child")).width == 190 &&
        d.bounds(d.find("inset-child")).height == 90,
        "theme mutation recomputes compiled variable inset");
  check(d.bounds(d.find("grid-left")).width == 195, "compiled grid variable updates with theme");
  check(d.bounds(d.find("variable-probe")).width == 195, "compiled var updates with theme");
  check(d.bounds(d.body()).x == 7,
        "theme attribute mutation updates root style");
  d.attribute(d.root(), "data-theme", "dark");
  d.render(800, 600);
  check(std::string(initial_scene.bytes.begin(), initial_scene.bytes.end())
                .find("Arial, sans-serif") != std::string::npos,
        "compiled font family reaches renderer");
  check(d.bounds(target).width == 60, "important beats inline");
  check(d.bounds(other).width == 30, "child selector");
  d.attribute(target, "class", "");
  d.render(800, 600);
  check(d.bounds(target).width == 70, "inline wins ordinary selector");
  d.focus(target);
  d.render(800, 600);
  check(d.bounds(target).height == 30, "focus selector");
  d.render(350, 600);
  check(d.bounds(target).height == 40, "responsive specificity");
  d.key("Tab");
  check(d.focused() == other, "forward tab");
  d.key("Tab", true);
  check(d.focused() == target, "reverse tab");
  d.focus(0);
  auto prevent =
      d.on(target, "pointerdown", [](event &e) { e.prevent_default(); });
  auto target_bounds = d.bounds(target);
  d.pointer("pointerdown", target_bounds.x + 2, target_bounds.y + 2);
  check(d.focused() == 0, "prevent default focus");
  prevent.dispose();
  int calls = 0;
  auto a = d.on(target, "click", [&](event &) { ++calls; });
  auto bounds = d.bounds(target);
  d.pointer("pointerdown", bounds.x + 2, bounds.y + 2);
  d.pointer("pointerup", bounds.x + 2, bounds.y + 2);
  check(calls == 1, "hit testing / bubble click");
  bool parent_called = false;
  auto parent = d.on(d.body(), "click", [&](event &) { parent_called = true; });
  auto stop = d.on(target, "click", [](event &e) { e.stop_propagation(); });
  d.dispatch(target, "click");
  check(!parent_called, "stop propagation");
  auto deleting = d.on(other, "click", [&](event &) { d.remove(other); });
  d.dispatch(other, "click");
  bool stale = false;
  try {
    d.bounds(other);
  } catch (const std::invalid_argument &) {
    stale = true;
  }
  check(stale, "remove during callback");
  std::atomic<bool> wrong_thread = false;
  std::thread worker([&] {
    try {
      d.element(d.body(), "div");
    } catch (const std::logic_error &) {
      wrong_thread = true;
    }
  });
  worker.join();
  check(wrong_thread, "thread confinement");
  auto canvas = refs.named("canvas");
  d.fill_rect(canvas, 1, 2, 3, 4, 0x123456ff);
  const auto &scene = d.render(350, 600);
  check(scene.layers.size() == 1, "canvas layer");
  check(scene.canvas.size() == 2 && scene.canvas[0].kind == 40 &&
            scene.canvas[1].kind == 22,
        "native canvas protocol");
  bool found_text = false;
  for (const auto &command : scene.commands)
    if (command.kind == 3) {
      check(command.flags < scene.strings.size(), "DOM string index");
      auto ref = scene.strings[command.flags];
      std::string text(scene.bytes.data() + ref.byte_offset, ref.byte_length);
      if (text.ends_with("\tGo"))
        found_text = true;
    }
  check(found_text, "canvas preserves DOM text table");
  auto layer = scene.layers[0];
  auto color = scene.strings[layer.string_offset];
  check(std::string(scene.bytes.data() + color.byte_offset,
                    color.byte_length) == "#123456ff",
        "canvas color table offset");
  d.clear_canvas(canvas);
  check(d.render(350, 600).layers.empty(), "canvas clearing");
  a.dispose();
  int before = calls;
  d.dispatch(target, "click");
  check(calls == before, "unsubscribe");
  auto shutdown = d.on(target, "close", [&](event &) { d.dispose(); });
  d.dispatch(target, "close");
  shutdown.dispose();
  bool disposed = false;
  try {
    d.render(100, 100);
  } catch (const std::logic_error &) {
    disposed = true;
  }
  check(disposed, "disposed document");
  {
    document grid_document;
    auto grid = grid_document.element(grid_document.body(), "div");
    grid_document.attribute(grid, "id", "workbench");
    auto left = grid_document.element(grid, "div");
    auto center = grid_document.element(grid, "div");
    auto right = grid_document.element(grid, "div");
    rule layout;
    selector_part part;
    part.id = "workbench";
    layout.match.parts.push_back(part);
    layout.declarations.push_back(
        {false, +[](style &s) {
           s.set_display(display_mode::grid);
           s.set_grid_template_columns(
               {{{222, length_unit::pixels},
                 {222, length_unit::pixels},
                 0,
                 grid_track::sizing::fixed},
                {{250, length_unit::pixels}, {}, 1, grid_track::sizing::minmax},
                {{252, length_unit::pixels},
                 {252, length_unit::pixels},
                 0,
                 grid_track::sizing::fixed}});
         }});
    grid_document.add_rule(std::move(layout));
    grid_document.render(1000, 600);
    check(std::abs(grid_document.bounds(left).width - 222) < 1,
          "compiled grid left track");
    check(std::abs(grid_document.bounds(right).width - 252) < 1,
          "compiled grid right track");
    auto old_width = grid_document.bounds(center).width;
    grid_document.render(1200, 600);
    check(std::abs(grid_document.bounds(center).width - old_width - 200) < 1,
          "compiled grid fractional track responds to resize");
  }
  {
    document states;
    auto button = states.element(states.body(), "button");
    states.set_text(button, "Press");
    rule active_rule;
    selector_part active_part;
    active_part.tag = "button";
    active_part.active = true;
    active_rule.match.parts.push_back(active_part);
    active_rule.declarations.push_back({false, +[](style &s) {
      s.set_width({123, length_unit::pixels});
    }});
    states.add_rule(std::move(active_rule));
    states.render(500, 300);
    auto area = states.bounds(button);
    states.pointer("pointerdown", area.x + 1, area.y + 1);
    states.render(500, 300);
    check(states.bounds(button).width == 123, "pressed selector applies");
    states.pointer("pointercancel", area.x + 1, area.y + 1);
    states.render(500, 300);
    check(states.bounds(button).width != 123, "cancel clears pressed selector");
    rule visible_rule;
    selector_part visible_part;
    visible_part.tag = "button";
    visible_part.focus_visible = true;
    visible_rule.match.parts.push_back(visible_part);
    visible_rule.declarations.push_back({false, +[](style &s) {
      s.set_width({140, length_unit::pixels});
    }});
    states.add_rule(std::move(visible_rule));
    states.render(500, 300);
    check(states.bounds(button).width != 140, "pointer focus has no keyboard indicator");
    states.key("Tab");
    states.render(500, 300);
    check(states.bounds(button).width == 140, "keyboard focus indicator applies");
    states.pointer("pointerdown", area.x + 1, area.y + 1);
    states.pointer("pointerup", area.x + 1, area.y + 1);
    states.render(500, 300);
    check(states.bounds(button).width != 140, "pointer switches keyboard modality");
    rule disabled_rule;
    selector_part disabled_part;
    disabled_part.tag = "button";
    disabled_part.disabled = true;
    disabled_rule.match.parts.push_back(disabled_part);
    disabled_rule.declarations.push_back({false, +[](style &s) {
      s.set_width({150, length_unit::pixels});
    }});
    states.add_rule(std::move(disabled_rule));
    states.attribute(button, "disabled", "");
    states.render(500, 300);
    check(states.bounds(button).width == 150, "disabled selector applies");
    states.attribute(button, "disabled", "false");
    states.focus(0);
    states.focus(button);
    check(states.focused() == 0, "boolean disabled uses presence, not string value");
    states.remove_attribute(button, "disabled");
    check(!states.attribute(button, "disabled"), "attribute removal clears presence");
    states.render(500, 300);
    check(states.bounds(button).width != 150, "attribute removal invalidates selector style");
    states.focus(button);
    check(states.focused() == button, "removing disabled restores focus eligibility");
    states.attribute(button, "id", "temporary-id");
    check(states.find("temporary-id") == button, "native id is searchable");
    states.remove_attribute(button, "id");
    check(states.find("temporary-id") == 0, "removing id clears native lookup state");
    states.remove_attribute(button, "missing");
  }
  {
    document tabs;
    auto ordinary = tabs.element(tabs.body(), "button");
    auto second = tabs.element(tabs.body(), "div");
    auto first = tabs.element(tabs.body(), "div");
    auto tied = tabs.element(tabs.body(), "div");
    auto negative = tabs.element(tabs.body(), "div");
    auto invalid = tabs.element(tabs.body(), "div");
    tabs.attribute(second, "tabindex", "2");
    tabs.attribute(first, "tabindex", " +1");
    tabs.attribute(tied, "tabindex", "1");
    tabs.attribute(negative, "tabindex", " -2");
    tabs.attribute(invalid, "tabindex", "bogus");
    tabs.render(400, 300);
    for (auto expected : {first, tied, second, ordinary, first}) {
      tabs.key("Tab");
      check(tabs.focused() == expected, "positive tabindex precedes ordinary order with stable ties");
    }
    tabs.key("Tab", true);
    check(tabs.focused() == ordinary, "reverse tab order wraps");
    tabs.focus(negative);
    check(tabs.focused() == negative, "negative tabindex remains programmatically focusable");
    tabs.focus(invalid);
    check(tabs.focused() == negative, "invalid tabindex does not make generic element focusable");
    tabs.remove_attribute(first, "tabindex");
    tabs.focus(0);
    tabs.key("Tab");
    check(tabs.focused() == tied, "tab order reflects removed tabindex");
  }
  {
    document visibility;
    auto container = visibility.element(visibility.body(), "div");
    auto child = visibility.element(container, "button");
    rule hidden;
    selector_part part;
    part.classes.push_back("hidden");
    hidden.match.parts.push_back(part);
    hidden.declarations.push_back({false, +[](style &s) { s.set_display(display_mode::none); }});
    visibility.add_rule(std::move(hidden));
    visibility.attribute(container, "class", "hidden");
    visibility.render(400, 300);
    visibility.focus(child);
    check(visibility.focused() == 0, "hidden ancestor prevents programmatic focus");
    visibility.remove_attribute(container, "class");
    visibility.render(400, 300);
    visibility.focus(child);
    check(visibility.focused() == child, "shown ancestor restores programmatic focus");
  }
  {
    document responsive;
    auto references = compiled_ui::build(responsive);
    responsive.render(1000, 301);
    check(responsive.bounds(responsive.find("flex-a")).width == 100 &&
          responsive.bounds(responsive.find("flex-b")).width == 200,
          "compiled flex shorthand distributes free space");
    check(responsive.bounds(responsive.find("negation")).width == 20, "compiled negation matches absent attribute");
    responsive.attribute(responsive.find("negation"), "data-excluded", "");
    responsive.render(1000, 301);
    check(responsive.bounds(responsive.find("negation")).width == 10, "attribute mutation updates negation");
    check(responsive.bounds(responsive.find("grid-left")).width == 222, "height media inactive above boundary");
    responsive.render(1000, 300);
    check(responsive.bounds(responsive.find("grid-left")).width == 175, "height media active at boundary");
    responsive.render(1000, 700);
    check(responsive.bounds(responsive.find("dynamic-viewport")).height == 350 &&
          responsive.bounds(responsive.find("dynamic-viewport")).width == 500,
          "dynamic viewport units track host dimensions");
    check(responsive.bounds(responsive.find("grid-left")).width == 222, "height media restores variable on resize");
  }
  {
    document structural;
    auto parent = structural.element(structural.body(), "div");
    structural.text(parent, " ");
    auto first = structural.element(parent, "button");
    rule last_rule;
    selector_part part;
    part.tag = "button";
    part.last_child = true;
    last_rule.match.parts.push_back(part);
    last_rule.declarations.push_back({false, +[](style &s) { s.set_width({99,length_unit::pixels}); }});
    structural.add_rule(std::move(last_rule));
    structural.render(500, 300);
    check(structural.bounds(first).width == 99, "last child ignores whitespace text");
    auto second = structural.element(parent, "button");
    structural.render(500, 300);
    check(structural.bounds(first).width != 99 && structural.bounds(second).width == 99, "insertion updates structural selector");
    structural.remove(second);
    structural.render(500, 300);
    check(structural.bounds(first).width == 99, "removal updates structural selector");
  }
  {
    document hit;
    compiled_ui::build(hit);
    auto front = hit.find("hit-front"), back = hit.find("hit-back");
    int target = 0;
    auto a = hit.on(front,"pointerdown",[&](event&){target=1;});
    auto b = hit.on(back,"pointerdown",[&](event&){target=2;});
    hit.render(1000,1500);
    auto bounds = hit.bounds(front);
    hit.pointer("pointerdown",bounds.x+5,bounds.y+5);
    check(target == 1,"compiled z-index determines pointer target");
    hit.attribute(front,"class","pass");
    hit.render(1000,1500);
    target=0;
    hit.pointer("pointerdown",bounds.x+5,bounds.y+5);
    check(target == 2,"compiled pointer-events passes through overlay");
  }
  std::cout << "Native Web contracts passed\n";
}
