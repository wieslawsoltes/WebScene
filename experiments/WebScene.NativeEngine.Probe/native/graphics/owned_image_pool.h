#pragma once
#include "image_lease_pool.h"
#include <memory>
#include <utility>

namespace webscene::graphics {
// Native provider lifetime anchor. Its destructor must be safe on a completion
// thread (thread-affine GPU destruction must be dispatched by the provider).
// No native pointer is exported through portable image metadata.
struct image_provider_lifetime {
    virtual ~image_provider_lifetime() = default;
};
class owned_image_pool {
    struct state {
        std::shared_ptr<image_provider_lifetime> provider;
        image_lease_pool pool;
        state(std::shared_ptr<image_provider_lifetime> p,size_t capacity,std::shared_ptr<completion_wake> wake)
            : provider(std::move(p)),pool(capacity,std::move(wake)) {
            if (!provider) throw std::invalid_argument("image provider required");
        }
    };
    std::shared_ptr<state> state_;
public:
    class consumer {
        std::shared_ptr<state> state_;
        image_lease_token token_{};
        friend class retained;
    public:
        consumer(std::shared_ptr<state> s,image_lease_token token):state_(std::move(s)),token_(token) {}
        consumer(const consumer&)=delete;
        consumer& operator=(const consumer&)=delete;
        consumer(consumer&&)=default;
        consumer& operator=(consumer&&)=delete;
        // Destroying a CPU wrapper is not evidence of GPU completion.
        ~consumer() { if (state_) std::terminate(); }
        image_metadata describe() const {
            if (!state_) throw std::invalid_argument("completed image consumer");
            return state_->pool.describe(token_);
        }
        std::shared_ptr<image_provider_lifetime> provider() const {
            if (!state_) throw std::invalid_argument("completed image consumer");
            return state_->provider;
        }
        void complete() {
            if (!state_) throw std::invalid_argument("duplicate image completion");
            state_->pool.finish_consumer(token_); state_.reset();
        }
    };
    class retained {
        std::shared_ptr<state> state_;
        image_lease_token token_{};
    public:
        retained(std::shared_ptr<state> s,image_lease_token token):state_(std::move(s)),token_(token) {}
        retained(const retained&)=delete;
        retained& operator=(const retained&)=delete;
        retained(retained&&)=default;
        retained& operator=(retained&&)=delete;
        ~retained() { if (state_) state_->pool.release(token_); }
        image_metadata describe() const {
            if (!state_) throw std::invalid_argument("moved image reference");
            return state_->pool.describe(token_);
        }
        std::optional<retained> retain() const {
            if (!state_) throw std::invalid_argument("moved image reference");
            auto token=state_->pool.retain(token_);
            if (!token) return {};
            return retained(state_,*token);
        }
        std::optional<consumer> begin_consumer() const {
            if (!state_) throw std::invalid_argument("moved image reference");
            auto token=state_->pool.begin_consumer(token_);
            if (!token) return {};
            return consumer(state_,*token);
        }
    };
    class producer {
        std::shared_ptr<state> state_;
        image_write_token token_{};
        bool started_{},completed_{},published_{};
    public:
        producer(std::shared_ptr<state> s,image_write_token token):state_(std::move(s)),token_(token) {}
        producer(const producer&)=delete;
        producer& operator=(const producer&)=delete;
        producer(producer&&)=default;
        producer& operator=(producer&&)=delete;
        ~producer() {
            if (!state_) return;
            if (started_ && !completed_) std::terminate();
            if (!published_) state_->pool.cancel_write(token_);
        }
        // Allocator-internal temporary reservations may cancel quietly to avoid
        // waking themselves for capacity they just borrowed and restored.
        void cancel(bool notify_capacity=true) {
            if (!state_) throw std::invalid_argument("moved image producer");
            state_->pool.cancel_write(token_,notify_capacity); state_.reset();
        }
        bool belongs_to(const image_provider_lifetime* provider) const noexcept {
            return state_ && state_->provider.get()==provider;
        }
        uint32_t slot() const {
            if (!state_) throw std::invalid_argument("moved image producer");
            return token_.slot;
        }
        void set_metadata(const image_metadata& metadata) {
            if (!state_) throw std::invalid_argument("moved image producer");
            state_->pool.set_metadata(token_,metadata);
        }
        void begin() {
            if (!state_) throw std::invalid_argument("moved image producer");
            state_->pool.begin_producer(token_); started_=true;
        }
        std::optional<retained> publish() {
            if (!state_) throw std::invalid_argument("moved image producer");
            auto token=state_->pool.publish(token_);
            if (!token) return {};
            published_=true; return retained(state_,*token);
        }
        void complete() {
            if (!state_) throw std::invalid_argument("moved image producer");
            state_->pool.finish_producer(token_); completed_=true;
        }
    };
    explicit owned_image_pool(std::shared_ptr<image_provider_lifetime> provider,size_t capacity=128,
        std::shared_ptr<completion_wake> wake={})
        : state_(std::make_shared<state>(std::move(provider),capacity,std::move(wake))) {}
    owned_image_pool(const owned_image_pool&)=delete;
    owned_image_pool& operator=(const owned_image_pool&)=delete;
    ~owned_image_pool() { close(); }
    std::optional<producer> acquire() {
        auto token=state_->pool.acquire_write();
        if (!token) return {};
        return producer(state_,*token);
    }
    void close() { state_->pool.close(); }
    size_t busy_images() const { return state_->pool.busy_images(); }
};
} // namespace webscene::graphics
