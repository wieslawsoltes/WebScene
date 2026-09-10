#pragma once
#include <v8.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace webscene::graphics {
// Engine/realm-owned views of an already mapped native buffer. Destroy/detach
// this object BEFORE unmapping or destroying native storage. The owner wrapper
// must in turn retain that storage; each reachable ArrayBuffer keeps it alive.
class v8_webgpu_mapped_ranges {
    struct range { uint64_t offset,length; v8::Global<v8::ArrayBuffer> view; };
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::Object> owner_;
    v8::Global<v8::Private> owner_key_;
    v8::Global<v8::Value> detach_key_;
    uint8_t* data_;
    uint64_t start_,length_;
    bool detached_{};
    std::vector<std::unique_ptr<range>> ranges_;
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("Mapped ranges require the owning isolate scope");
    }
public:
    v8_webgpu_mapped_ranges(v8::Isolate* isolate,v8::Local<v8::Context> context,
        v8::Local<v8::Object> owner,void* data,uint64_t start,uint64_t length)
        :isolate_(isolate),data_(static_cast<uint8_t*>(data)),start_(start),length_(length) {
        check_scope();
        if (owner.IsEmpty() || (!data && length) || length>SIZE_MAX || start>UINT64_MAX-length)
            throw std::invalid_argument("Invalid native mapped region");
        realm_.Reset(isolate,context);
        owner_.Reset(isolate,owner); owner_.SetWeak();
        owner_key_.Reset(isolate,v8::Private::New(isolate));
        detach_key_.Reset(isolate,v8::Object::New(isolate));
    }
    v8_webgpu_mapped_ranges(const v8_webgpu_mapped_ranges&)=delete;
    v8_webgpu_mapped_ranges& operator=(const v8_webgpu_mapped_ranges&)=delete;
    ~v8_webgpu_mapped_ranges() { detach(); }
    v8::MaybeLocal<v8::ArrayBuffer> create(v8::Local<v8::Context> context,uint64_t offset,uint64_t length) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("Mapped range belongs to another realm");
        if (detached_ || owner_.IsEmpty() || offset%8 || length%4 || offset<start_
            || offset-start_>length_ || length>length_-(offset-start_))
            throw std::invalid_argument("Invalid mapped buffer range");
        // Even collected views reserve their ranges until unmap. Empty ranges
        // occupy no bytes and therefore never overlap another range.
        for (const auto& prior:ranges_) if (length && prior->length
            && offset<prior->offset+prior->length && prior->offset<offset+length)
            throw std::invalid_argument("Mapped buffer ranges overlap");
        auto item=std::make_unique<range>(); item->offset=offset; item->length=length;
        if (ranges_.size()==ranges_.capacity())
            ranges_.reserve(ranges_.empty() ? 8 : ranges_.size()*2);
        auto backing=v8::ArrayBuffer::NewBackingStore(length ? data_+(offset-start_) : nullptr,
            static_cast<size_t>(length),[](void*,size_t,void*) {},nullptr);
        auto view=v8::ArrayBuffer::New(isolate_,std::move(backing));
        if (!view->SetPrivate(context,owner_key_.Get(isolate_),owner_.Get(isolate_)).FromMaybe(false)) return {};
        view->SetDetachKey(detach_key_.Get(isolate_));
        item->view.Reset(isolate_,view); item->view.SetWeak();
        ranges_.push_back(std::move(item));
        return view;
    }
    void detach() {
        check_scope();
        if (detached_) return;
        for (auto& item:ranges_) if (!item->view.IsEmpty()) {
            auto view=item->view.Get(isolate_);
            if (!view->WasDetached() && !view->Detach(detach_key_.Get(isolate_)).FromMaybe(false))
                throw std::logic_error("Mapped ArrayBuffer detachment failed");
            if (!view->DeletePrivate(realm_.Get(isolate_),owner_key_.Get(isolate_)).FromMaybe(false))
                throw std::logic_error("Mapped ArrayBuffer owner release failed");
            item->view.Reset();
        }
        ranges_.clear(); detached_=true; data_=nullptr;
    }
};
} // namespace webscene::graphics
