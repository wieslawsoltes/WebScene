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
    require(box.has_pending());
    require(!box.publish(first,completion_status::success));
    require(box.drain_one([](auto record) { require(record.operation==2 && record.status==completion_status::success); }));
    require(box.drain_one([](auto record) { require(record.operation==1 && record.status==completion_status::cancelled); }));
    auto reused=box.reserve(3,a).value();
    require(!box.publish(first,completion_status::success));
    require(box.has_pending());
    box.close();
    require(box.has_pending());
    require(!box.publish(reused,completion_status::success) && !box.reserve(4,b));
    require(box.drain_one([](auto record) { require(record.operation==3 && record.status==completion_status::cancelled); }));
    require(!box.has_ready());
    auto counters=box.metrics();
    require(counters.pending==0 && counters.ready==0 && counters.high_water==2);
    require(counters.admitted==3 && counters.delivered==3 && counters.saturated_reservations==1);
    require(counters.rejected_publications==4 && counters.latency_samples==0);
    completion_mailbox delayed(1,wake);
    auto cancelled=delayed.reserve(1,a).value();
    delayed.cancel_owner(a);
    require(delayed.drain_one([](auto record) { require(record.status==completion_status::cancelled); }));
    require(delayed.has_pending() && !delayed.reserve(2,a));
    require(delayed.metrics().occupied==1 && delayed.metrics().native_pending==1);
    std::thread late([&] { require(!delayed.publish(cancelled,completion_status::success)); });
    late.join();
    require(!delayed.has_pending() && delayed.metrics().occupied==0);
    auto next=delayed.reserve(2,a).value();
    require(next.generation!=cancelled.generation);
    require(!delayed.publish(cancelled,completion_status::success));
    require(delayed.publish(next,completion_status::success));
    require(delayed.drain_one([](auto) {}));
    completion_mailbox dormant(2,wake);
    auto lifetime=dormant.reserve(1,a,false).value();
    require(dormant.has_pending()&&!dormant.has_pollable_pending()&&!dormant.has_ready());
    auto active=dormant.reserve(2,b).value();
    require(dormant.has_pollable_pending());
    require(dormant.publish(active,completion_status::success));
    require(dormant.drain_one([](auto){}));
    require(!dormant.has_pollable_pending());
    require(dormant.publish(lifetime,completion_status::success)&&dormant.has_ready());
    require(dormant.drain_one([](auto){}));
    require(!dormant.has_pending());
    auto cancel_lifetime=dormant.reserve(3,a,false).value();
    dormant.cancel_owner(a);
    require(dormant.has_ready()&&!dormant.has_pollable_pending());
    require(dormant.drain_one([](auto record){require(record.status==completion_status::cancelled);}));
    require(!dormant.publish(cancel_lifetime,completion_status::success)&&!dormant.has_pending());
    completion_mailbox timed(1,wake,true);
    auto measured=timed.reserve(1,a).value();
    require(timed.publish(measured,completion_status::success));
    require(timed.drain_one([](auto) {}));
    auto timings=timed.metrics();
    require(timings.latency_samples==1 && timings.total_latency_ns==timings.max_latency_ns);
    require(timings.pending==0 && timings.ready==0);
    std::cout << "completion capacity, engine affinity, isolation and late-callback cancellation passed\n";
}
