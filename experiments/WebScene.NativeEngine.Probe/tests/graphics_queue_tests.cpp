#include "graphics/work_queue.h"
#include <array>
#include <atomic>
#include <iostream>
#include <thread>
using namespace webscene::graphics;
void require(bool value) { if (!value) throw std::runtime_error("requirement failed"); }
int main() {
    work_queue<uint64_t> queue(2, 4);
    std::array<std::byte, 4> source{std::byte{42}};
    require(queue.try_push(1, source) == enqueue_result::accepted);
    source[0] = std::byte{99};
    require(queue.try_push(2) == enqueue_result::accepted);
    require(queue.try_push(3) == enqueue_result::full);
    queue.consume_one([&](auto command, auto upload, auto serial) {
        require(command == 1 && serial == 1 && upload[0] == std::byte{42});
        require(queue.try_push(3) == enqueue_result::full);
    });
    require(queue.try_push(3) == enqueue_result::accepted);
    queue.close();
    require(queue.try_push(4) == enqueue_result::closed);
    for (uint64_t expected = 2; expected <= 3; ++expected)
        require(queue.consume_one([&](auto command, auto, auto serial) { require(command == expected && serial == expected); }));
    require(queue.metrics().depth == 0 && queue.metrics().high_water == 2 && queue.metrics().upload_bytes == 4);
    // Saturated producer and consumer progress without RAF, UI or GPU callbacks.
    work_queue<uint64_t> concurrent(8, 0);
    constexpr uint64_t count = 100000;
    std::thread producer([&] {
        for (uint64_t i = 1; i <= count; ++i)
            while (concurrent.try_push(i) == enqueue_result::full) std::this_thread::yield();
        concurrent.close();
    });
    uint64_t expected = 1;
    while (expected <= count) {
        if (!concurrent.consume_one([&](auto command, auto, auto serial) {
            require(command == expected && serial == expected); ++expected;
        })) std::this_thread::yield();
    }
    producer.join();
    require(concurrent.metrics().depth == 0 && concurrent.metrics().high_water <= 8);
    // Multiple producers copy call-time bytes into bounded slots. Admission
    // serials define the global order; each producer's own sequence stays FIFO.
    work_queue<uint64_t> uploads(8,32);
    constexpr uint64_t producers=4, per_producer=5000;
    std::vector<std::thread> writers;
    for(uint64_t writer=0;writer<producers;++writer) writers.emplace_back([&,writer] {
        for(uint64_t sequence=1;sequence<=per_producer;++sequence) {
            const auto command=writer*per_producer+sequence;
            std::array<std::byte,32> bytes;
            for(size_t index=0;index<bytes.size();++index)
                bytes[index]=static_cast<std::byte>((command+index)%251);
            enqueue_result result;
            do {
                result=uploads.try_push(command,bytes);
                if(result==enqueue_result::full) std::this_thread::yield();
            } while(result==enqueue_result::full);
            require(result==enqueue_result::accepted);
            bytes.fill(std::byte{255}); // Mutation cannot change an accepted upload.
        }
    });
    std::array<uint64_t,producers> sequences{};
    uint64_t delivered=0;
    while(delivered<producers*per_producer) {
        if(!uploads.consume_one([&](auto command,auto bytes,auto serial) {
            require(serial==delivered+1 && bytes.size()==32);
            const auto writer=(command-1)/per_producer;
            const auto sequence=(command-1)%per_producer+1;
            require(writer<producers && sequence==++sequences[writer]);
            for(int pass=0;pass<2;++pass) {
                for(size_t index=0;index<bytes.size();++index)
                    require(bytes[index]==static_cast<std::byte>((command+index)%251));
                std::this_thread::yield(); // Slot must stay owned during consumption.
            }
            ++delivered;
        })) std::this_thread::yield();
    }
    for(auto& writer:writers) writer.join();
    uploads.close();
    const auto upload_metrics=uploads.metrics();
    require(upload_metrics.depth==0 && upload_metrics.high_water<=8);
    require(upload_metrics.accepted==producers*per_producer);
    require(upload_metrics.upload_bytes==producers*per_producer*32);
    std::cout << "graphics queue call-time copy, FIFO, backpressure and concurrent stress passed\n";
}
