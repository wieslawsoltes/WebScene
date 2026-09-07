#include "graphics/image_lease_pool.h"
#include <iostream>
using namespace webscene::graphics;
void require(bool v) { if (!v) throw std::runtime_error("image lease requirement failed"); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch(const std::invalid_argument&) { failed=true; } require(failed); }
int main() {
    image_lease_pool pool;
    auto a=pool.acquire_write().value(),b=pool.acquire_write().value(),c=pool.acquire_write().value();
    require(!pool.acquire_write() && pool.busy_images()==3);
    auto scene=pool.publish(a).value();
    auto retained=pool.retain(scene).value();
    auto gpu=pool.begin_consumer(scene).value();
    auto gpu_second=pool.begin_consumer(scene).value();
    pool.release(scene);
    rejects([&] { pool.release(scene); });
    pool.finish_producer(a);
    pool.release(retained);
    require(!pool.acquire_write()); // Scene release cannot finish consumer work.
    rejects([&] { pool.release(gpu); });
    std::thread presenter([&] { pool.finish_consumer(gpu); }); presenter.join();
    rejects([&] { pool.finish_consumer(gpu); });
    require(!pool.acquire_write());
    pool.finish_consumer(gpu_second);
    auto reused=pool.acquire_write().value();
    require(reused.slot==a.slot && reused.generation!=a.generation);
    rejects([&] { pool.finish_producer(a); });
    rejects([&] { pool.finish_consumer(gpu); });
    pool.cancel_write(reused); pool.cancel_write(b); pool.cancel_write(c);
    require(pool.busy_images()==0);
    auto frame=pool.acquire_write().value();
    auto reference=pool.publish(frame).value();
    auto consumer=pool.begin_consumer(reference).value();
    pool.close();
    require(!pool.acquire_write());
    auto redraw=pool.retain(reference).value(); // Retained redraw survives close.
    pool.release(reference); pool.release(redraw); pool.finish_consumer(consumer);
    require(pool.busy_images()==1); // Producer notification can arrive last.
    pool.finish_producer(frame); require(pool.busy_images()==0);
    image_lease_pool bounded(1);
    auto writer=bounded.acquire_write().value(),waiting=bounded.acquire_write().value();
    auto lease=bounded.publish(writer).value();
    require(!bounded.retain(lease) && !bounded.begin_consumer(lease) && !bounded.publish(waiting));
    bounded.release(lease); bounded.finish_producer(writer);
    bounded.finish_producer(waiting); // GPU may finish before CPU publication.
    auto published=bounded.publish(waiting).value();
    bounded.release(published);
    require(bounded.busy_images()==0);
    std::cout << "three-slot backpressure, retained scenes and independent GPU completion passed\n";
}
