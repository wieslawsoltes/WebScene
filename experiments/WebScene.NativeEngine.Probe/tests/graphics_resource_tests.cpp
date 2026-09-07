#include "graphics/resource_table.h"
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected);
}
struct tracked {
    int& live;
    explicit tracked(int& count) : live(count) { ++live; }
    ~tracked() { --live; }
};
int main() {
    int live = 0;
    resource_owner a{new_owner_token(), new_owner_token(), new_owner_token()};
    auto b = a; b.device = new_owner_token();
    resource_table<tracked> table(2), foreign(2);
    auto first = table.insert(a, std::make_unique<tracked>(live));
    rejects([&] { table.get(first, b); });
    auto wrong_context = a; wrong_context.context = new_owner_token();
    rejects([&] { table.get(first, wrong_context); });
    rejects([&] { foreign.get(first, a); });
    bool wrong_thread = false;
    std::thread worker([&] { try { table.get(first, a); } catch (const std::logic_error&) { wrong_thread = true; } });
    worker.join(); require(wrong_thread);
    table.mark_used(first, a, 2);
    table.destroy(first, a);
    rejects([&] { table.get(first, a); });
    require(live == 1 && table.deferred_count() == 1);
    auto second = table.insert(b, std::make_unique<tracked>(live));
    rejects([&] { table.insert(a, std::make_unique<tracked>(live)); });
    table.complete(1); require(live == 2);
    table.complete(2); require(live == 1 && table.deferred_count() == 0);
    auto reused = table.insert(a, std::make_unique<tracked>(live));
    require(reused.slot == first.slot && reused.generation != first.generation);
    rejects([&] { table.get(first, a); });
    table.destroy_owner(a); require(live == 1);
    table.get(second, b);
    rejects([&] { table.complete(1); });
    table.destroy(second, b);
    require(live == 0 && table.resident_count() == 0);
    std::cout << "graphics resource lifetime and owner isolation passed\n";
}
