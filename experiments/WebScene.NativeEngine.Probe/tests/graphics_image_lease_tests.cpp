#include "graphics/image_lease_pool.h"
#include <iostream>
using namespace webscene::graphics;
void require(bool v) { if (!v) throw std::runtime_error("image lease requirement failed"); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch(const std::invalid_argument&) { failed=true; } require(failed); }
image_metadata metadata{1,2,1,3,4,5,640,480};
image_write_token write(image_lease_pool& pool) {
    auto writer=pool.acquire_write().value();
    pool.set_metadata(writer,metadata);
    return writer;
}
int main() {
    image_lease_pool pool;
    auto a=write(pool),b=write(pool),c=write(pool);
    require(!pool.acquire_write() && pool.busy_images()==3);
    auto scene=pool.publish(a).value();
    auto retained=pool.retain(scene).value();
    auto gpu=pool.begin_consumer(scene).value();
    auto gpu_second=pool.begin_consumer(scene).value();
    require(pool.describe(gpu).allocation==metadata.allocation);
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
    auto reference=pool.publish(frame).value();
    auto consumer=pool.begin_consumer(reference).value();
    pool.close();
    require(!pool.acquire_write());
    auto redraw=pool.retain(reference).value(); // Retained redraw survives close.
    pool.release(reference); pool.release(redraw); pool.finish_consumer(consumer);
    require(pool.busy_images()==1); // Producer notification can arrive last.
    pool.finish_producer(frame); require(pool.busy_images()==0);
    image_lease_pool bounded(1);
    auto writer=write(bounded),waiting=write(bounded);
    auto lease=bounded.publish(writer).value();
    require(!bounded.retain(lease) && !bounded.begin_consumer(lease) && !bounded.publish(waiting));
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
    image_lease_pool resize;
    auto old_frame=write(resize);
    auto old_scene=resize.publish(old_frame).value();
    auto new_frame=write(resize);
    auto resized=metadata; resized.width=800; resized.allocation=6; resized.allocation_generation=2;
    resize.set_metadata(new_frame,resized);
    auto new_scene=resize.publish(new_frame).value();
    require(resize.describe(old_scene).width==640 && resize.describe(old_scene).allocation_generation==1);
    require(resize.describe(new_scene).width==800 && resize.describe(new_scene).allocation_generation==2);
    rejects([&] { pool.describe(old_scene); });
    resize.release(old_scene); resize.finish_producer(old_frame);
    resize.finish_producer(new_frame); resize.release(new_scene);
    require(resize.busy_images()==0);
    std::cout << "three-slot backpressure, retained scenes and independent GPU completion passed\n";
}
