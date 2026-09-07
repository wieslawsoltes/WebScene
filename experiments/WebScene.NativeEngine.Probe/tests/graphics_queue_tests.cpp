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
    std::cout << "graphics queue call-time copy, FIFO, backpressure and concurrent stress passed\n";
}
