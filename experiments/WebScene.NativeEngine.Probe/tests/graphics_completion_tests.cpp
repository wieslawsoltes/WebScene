#include "graphics/completion_mailbox.h"
#include "graphics/producer_completion_gate.h"
#include <iostream>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
struct wake_counter : completion_wake {
    std::atomic<int> count{};
    void signal() noexcept override { ++count; }
};
int main() {
    // Hold either producer phase indefinitely. No readiness or release signal
    // is permitted until the other phase arrives, including on validation error.
    for(bool queue_first:{false,true})for(bool queue_ok:{false,true})for(bool validation_ok:{false,true}) {
        producer_completion_gate gate;
        const auto first=queue_first ? producer_completion_gate::phase::queue : producer_completion_gate::phase::validation;
        const auto second=queue_first ? producer_completion_gate::phase::validation : producer_completion_gate::phase::queue;
        require(!gate.validated_for_gpu_wait());
        require(!gate.finish(first,queue_first ? queue_ok : validation_ok));
        require(gate.validated_for_gpu_wait()==(!queue_first && validation_ok));
        for(int poll=0;poll<100;++poll)require(gate.state()==producer_completion_gate::result::pending);
        require(!gate.finish(first,true)); // A duplicate cannot supply the missing phase.
        require(gate.state()==producer_completion_gate::result::pending);
        require(gate.finish(second,queue_first ? validation_ok : queue_ok));
        const auto expected=queue_ok&&validation_ok ? producer_completion_gate::result::success : producer_completion_gate::result::failure;
        require(gate.state()==expected);
        require(gate.validated_for_gpu_wait()==(queue_ok && validation_ok));
        require(!gate.finish(first,false)&&!gate.finish(second,false));
        require(gate.state()==expected);
    }
    producer_completion_gate rejected;
    rejected.reject();
    require(!rejected.finish(producer_completion_gate::phase::queue,true));
    require(rejected.finish(producer_completion_gate::phase::validation,true));
    require(rejected.state()==producer_completion_gate::result::failure);

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
    // Saturate reusable storage while driver completion races owner cancellation.
    // Delivery stays on this engine thread; every native callback must retire,
    // even if its logical cancellation was already delivered.
    completion_mailbox stress(16,wake);
    for(uint64_t round=0;round<1000;++round) {
        std::vector<completion_ticket> tickets;
        for(uint64_t index=0;index<16;++index)
            tickets.push_back(stress.reserve(round*16+index,index%2 ? b : a).value());
        require(!stress.reserve(UINT64_MAX,a));
        std::atomic<bool> start{false};
        std::thread driver([&] {
            while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for(auto ticket:tickets) {
                stress.publish(ticket,completion_status::success);
                std::this_thread::yield();
            }
        });
        start.store(true,std::memory_order_release);
        stress.cancel_owner(a);
        bool seen[16]{};
        size_t delivered=0;
        const auto deliver=[&](auto record) {
            require(record.operation/16==round);
            const auto index=record.operation%16;
            require(!seen[index]);seen[index]=true;++delivered;
            require(record.status==(index%2 ? completion_status::success : completion_status::cancelled));
        };
        while(stress.drain_one(deliver)) {}
        driver.join();
        while(stress.drain_one(deliver)) {}
        require(delivered==16);
        const auto metrics=stress.metrics();
        require(metrics.pending==0 && metrics.ready==0 && metrics.native_pending==0 && metrics.occupied==0);
        require(!stress.publish(tickets.front(),completion_status::success));
    }
    require(stress.metrics().admitted==16000 && stress.metrics().delivered==16000);
    require(stress.metrics().high_water==16 && stress.metrics().saturated_reservations==1000);
    std::cout << "completion capacity, engine affinity, isolation and late-callback cancellation passed\n";
}
