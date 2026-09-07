#include "graphics/completion_mailbox.h"
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
struct wake_counter : completion_wake {
    std::atomic<int> count{};
    void signal() noexcept override { ++count; }
};
int main() {
    auto wake = std::make_shared<wake_counter>();
    completion_mailbox box(2, wake);
    resource_owner a{new_owner_token(),new_owner_token(),0};
    auto b=a; b.device=new_owner_token();
    auto first=box.reserve(1,a).value(), second=box.reserve(2,b).value();
    require(!box.reserve(3,a) && box.has_pending());
    bool rejected_thread=false;
    std::thread callback([&] {
        require(box.publish(second,completion_status::success));
        require(!box.publish(second,completion_status::success));
        try { box.drain_one([](auto) {}); } catch(const std::logic_error&) { rejected_thread=true; }
    }); callback.join();
    require(rejected_thread && wake->count==1 && box.has_pending());
    box.cancel_owner(a);
    require(!box.has_pending());
    require(!box.publish(first,completion_status::success));
    require(box.drain_one([](auto record) { require(record.operation==2 && record.status==completion_status::success); }));
    require(box.drain_one([](auto record) { require(record.operation==1 && record.status==completion_status::cancelled); }));
    auto reused=box.reserve(3,a).value();
    require(!box.publish(first,completion_status::success));
    require(box.has_pending());
    box.close();
    require(!box.has_pending());
    require(!box.publish(reused,completion_status::success) && !box.reserve(4,b));
    require(box.drain_one([](auto record) { require(record.operation==3 && record.status==completion_status::cancelled); }));
    require(!box.has_ready());
    std::cout << "completion capacity, engine affinity, isolation and late-callback cancellation passed\n";
}
