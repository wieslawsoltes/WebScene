#include <foco/foco.hpp>
#include <iostream>
#include <stdexcept>
class document_surface final : public foco::control {
public:
  bool accept{true}, reverse{};
  int moves{};
  document_surface() { set_focusable(true); }
  bool try_move_focus_within(bool backwards) override {
    ++moves;
    reverse = backwards;
    return accept;
  }
};
static void check(bool value) {
  if (!value)
    throw std::runtime_error("embedded focus contract");
}
int main() {
  auto root = foco::make_ref<foco::window>();
  auto first = foco::make_ref<document_surface>();
  auto next = foco::make_ref<foco::button>();
  root->add_child(first);
  root->add_child(next);
  foco::raw_input_queue queue;
  foco::input_router router(*root, queue);
  check(router.focus().focus(*first));
  check(router.route_key({.value = foco::key::tab}));
  check(first->moves == 1 && !first->reverse &&
        router.focus().focused() == first.get());
  check(router.route_key(
      {.value = foco::key::tab, .modifiers = foco::key_modifiers::shift}));
  check(first->moves == 2 && first->reverse &&
        router.focus().focused() == first.get());
  first->accept = false;
  check(router.route_key({.value = foco::key::tab}));
  check(router.focus().focused() == next.get());
  check(router.route_key(
      {.value = foco::key::tab, .modifiers = foco::key_modifiers::shift}));
  check(router.focus().focused() == first.get());
  std::cout << "Foco embedded focus navigation passed\n";
}
