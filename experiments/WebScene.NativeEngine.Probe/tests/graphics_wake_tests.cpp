#include "graphics/engine_wake.h"
#include <future>
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
int main() {
    auto wake=std::make_shared<engine_wake>();
    wake->signal();
    require(wake->wait_for(std::chrono::milliseconds(0),[] { return false; }));
    require(!wake->wait_for(std::chrono::milliseconds(0),[] { return false; }));
    std::promise<void> waiting;
    auto future=std::async(std::launch::async,[wake,&waiting] {
        waiting.set_value();
        return wake->wait_for(std::chrono::seconds(5),[] { return false; });
    });
    waiting.get_future().wait();
    wake->signal();
    require(future.get());
    std::shared_ptr<completion_wake> late=wake;
    wake.reset();
    late->signal();
    std::cout << "engine wake latching, headless progress and retained signal lifetime passed\n";
}
