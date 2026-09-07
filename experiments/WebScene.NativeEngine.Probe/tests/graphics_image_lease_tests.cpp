#include "graphics/image_lease_pool.h"
#include "graphics/owned_image_pool.h"
#include "graphics/engine_wake.h"
#include <atomic>
#include <iostream>
using namespace webscene::graphics;
void require(bool v) { if (!v) throw std::runtime_error("image lease requirement failed"); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch(const std::invalid_argument&) { failed=true; } require(failed); }
image_metadata metadata{1,2,1,3,4,5,640,480};
image_write_token write(image_lease_pool& pool) {
    auto writer=pool.acquire_write().value();
    auto physical=metadata; physical.allocation=10+writer.slot;
    pool.set_metadata(writer,physical);
    return writer;
}
std::optional<image_lease_token> submit(image_lease_pool& pool,image_write_token writer) {
    pool.begin_producer(writer);
    return pool.publish(writer);
}
void test_capacity_wake() {
    struct capacity_wake final : completion_wake {
        image_lease_pool* pool{};
        engine_wake latched;
        size_t signals{},last_busy{};
        void signal() noexcept override {
            last_busy=pool->busy_images(); // Must run outside the pool mutex.
            ++signals; latched.signal();
        }
    };
    auto wake=std::make_shared<capacity_wake>();
    image_lease_pool pool(1,wake); wake->pool=&pool;
    auto writer=write(pool); auto retained=submit(pool,writer).value();
    require(!pool.retain(retained) && !pool.begin_consumer(retained));
    pool.finish_producer(writer); require(wake->signals==0); // No new capacity yet.
    std::thread release([&] { pool.release(retained); }); release.join();
    require(wake->signals==1 && wake->last_busy==0);
    require(wake->latched.wait_for(std::chrono::milliseconds(0),[] { return false; }));
    auto abandoned=write(pool); pool.cancel_write(abandoned);
    require(wake->signals==2);
    writer=write(pool); retained=submit(pool,writer).value();
    pool.release(retained); require(wake->signals==3 && wake->last_busy==1);
    pool.finish_producer(writer); require(wake->signals==4 && wake->last_busy==0);
    rejects([&] { pool.finish_producer(writer); }); require(wake->signals==4);
}
void test_owned_lifetime() {
    struct provider final : image_provider_lifetime {
        std::atomic<int>& destroyed;
        explicit provider(std::atomic<int>& count):destroyed(count) {}
        ~provider() override { ++destroyed; }
    };
    std::atomic<int> destroyed=0;
    auto backend=std::make_shared<provider>(destroyed);
    std::weak_ptr<provider> weak=backend;
    auto owner=std::make_unique<owned_image_pool>(backend);
    backend.reset();
    auto producer=owner->acquire();
    producer->set_metadata(metadata); producer->begin();
    auto scene=producer->publish();
    auto redraw=scene->retain();
    auto pending=scene->begin_consumer();
    scene.reset();
    owner.reset(); // Engine/canvas ownership ends; GPU use is still outstanding.
    require(!weak.expired() && destroyed==0);
    producer->complete(); producer.reset();
    require(redraw->describe().allocation==metadata.allocation);
    auto second=redraw->begin_consumer(); // Retained redraw after engine disposal.
    redraw.reset();
    pending->complete(); pending.reset();
    require(!weak.expired() && destroyed==0);
    std::thread completion([use=std::move(*second)]() mutable {
        require(use.describe().content_serial==metadata.content_serial);
        use.complete();
        rejects([&] { use.complete(); });
    });
    second.reset(); completion.join();
    require(weak.expired() && destroyed==1);

    auto bounded=std::make_unique<owned_image_pool>(std::make_shared<provider>(destroyed),1);
    { auto abandoned=bounded->acquire(); } // Unsubmitted writers cancel automatically.
    require(bounded->busy_images()==0);
    auto frame=bounded->acquire(); frame->set_metadata(metadata); frame->begin();
    auto held=frame->publish();
    require(!held->retain() && !held->begin_consumer()); // Ticket backpressure.
    frame->complete(); frame.reset();
    require(bounded->busy_images()==1);
    held.reset(); require(bounded->busy_images()==0);
    bounded->close(); require(!bounded->acquire());
    bounded.reset(); require(destroyed==2);
}
void test_cache_eviction_reservations() {
    auto provider=std::make_shared<image_provider_lifetime>();
    owned_image_pool pool(provider);
    auto producer=pool.acquire();
    const auto protected_slot=producer->slot();
    producer->set_metadata(metadata); producer->begin();
    auto scene=producer->publish();
    auto consumer=scene->begin_consumer();
    producer->complete(); producer.reset(); scene.reset();
    // A GPU consumer alone protects its allocation from cache eviction.
    auto first=pool.acquire(); auto second=pool.acquire();
    require(first && second && first->slot()!=second->slot()
        && first->slot()!=protected_slot && second->slot()!=protected_slot && !pool.acquire());
    first->cancel(false); second->cancel(false);
    require(pool.busy_images()==1);
    consumer->complete(); consumer.reset();
    auto reusable=pool.acquire();
    require(reusable && reusable->slot()==protected_slot);
}
int main() {
    test_cache_eviction_reservations();
    test_capacity_wake();
    test_owned_lifetime();
    image_lease_pool pool;
    auto a=write(pool),b=write(pool),c=write(pool);
    require(!pool.acquire_write() && pool.busy_images()==3);
    auto scene=submit(pool,a).value();
    auto retained=pool.retain(scene).value();
    auto gpu=pool.begin_consumer(scene).value();
    auto gpu_second=pool.begin_consumer(scene).value();
    require(pool.describe(gpu).allocation==10+a.slot);
    rejects([&] { pool.set_metadata(a,metadata); });
    pool.release(scene);
    rejects([&] { pool.describe(scene); });
    rejects([&] { pool.release(scene); });
    pool.finish_producer(a);
    pool.release(retained);
    require(pool.describe(gpu).width==640 && pool.describe(gpu).producer_timeline==4);
    require(!pool.acquire_write()); // Scene release cannot finish consumer work.
    rejects([&] { pool.release(gpu); });
    std::thread presenter([&] { pool.finish_consumer(gpu); }); presenter.join();
    rejects([&] { pool.finish_consumer(gpu); });
    require(!pool.acquire_write());
    pool.finish_consumer(gpu_second);
    auto reused=write(pool);
    require(reused.slot==a.slot && reused.generation!=a.generation);
    rejects([&] { pool.finish_producer(a); });
    rejects([&] { pool.finish_consumer(gpu); });
    pool.cancel_write(reused); pool.cancel_write(b); pool.cancel_write(c);
    require(pool.busy_images()==0);
    auto frame=write(pool);
    auto reference=submit(pool,frame).value();
    auto consumer=pool.begin_consumer(reference).value();
    pool.close();
    require(!pool.acquire_write());
    auto redraw=pool.retain(reference).value(); // Retained redraw survives close.
    pool.release(reference); pool.release(redraw); pool.finish_consumer(consumer);
    require(pool.busy_images()==1); // Producer notification can arrive last.
    pool.finish_producer(frame); require(pool.busy_images()==0);
    image_lease_pool bounded(1);
    auto writer=write(bounded),waiting=write(bounded);
    auto lease=submit(bounded,writer).value();
    require(!bounded.retain(lease) && !bounded.begin_consumer(lease) && !submit(bounded,waiting));
    bounded.release(lease); bounded.finish_producer(writer);
    bounded.finish_producer(waiting); // GPU may finish before CPU publication.
    auto published=bounded.publish(waiting).value();
    bounded.release(published);
    require(bounded.busy_images()==0);
    image_lease_pool invalid;
    auto unconfigured=invalid.acquire_write().value();
    rejects([&] { invalid.publish(unconfigured); });
    auto invalid_metadata=metadata; invalid_metadata.width=0;
    rejects([&] { invalid.set_metadata(unconfigured,invalid_metadata); });
    invalid_metadata=metadata; invalid_metadata.format=static_cast<image_format>(999);
    rejects([&] { invalid.set_metadata(unconfigured,invalid_metadata); });
    invalid.cancel_write(unconfigured);
    auto abandoned=write(invalid);
    rejects([&] { invalid.finish_producer(abandoned); });
    invalid.begin_producer(abandoned);
    rejects([&] { invalid.begin_producer(abandoned); });
    rejects([&] { invalid.cancel_write(abandoned); });
    rejects([&] { invalid.set_metadata(abandoned,metadata); });
    invalid.close();
    require(invalid.busy_images()==1);
    invalid.finish_producer(abandoned);
    invalid.cancel_write(abandoned);
    require(invalid.busy_images()==0);
    image_lease_pool resize;
    auto old_frame=write(resize);
    auto old_scene=submit(resize,old_frame).value();
    auto new_frame=write(resize);
    auto resized=metadata; resized.width=800; resized.allocation=6; resized.allocation_generation=2;
    resize.set_metadata(new_frame,resized);
    auto alias=resize.describe(old_scene);
    alias.allocation_generation=99;
    rejects([&] { resize.set_metadata(new_frame,alias); });
    auto new_scene=submit(resize,new_frame).value();
    require(resize.describe(old_scene).width==640 && resize.describe(old_scene).allocation_generation==1);
    require(resize.describe(new_scene).width==800 && resize.describe(new_scene).allocation_generation==2);
    rejects([&] { pool.describe(old_scene); });
    resize.release(old_scene); resize.finish_producer(old_frame);
    resize.finish_producer(new_frame); resize.release(new_scene);
    require(resize.busy_images()==0);
    std::cout << "three-slot backpressure, retained scenes and independent GPU completion passed\n";
}
