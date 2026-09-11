#pragma once
#include <array>
#include <cstddef>

namespace webscene::graphics {
// Access is serialized by the submission owner. Failure still waits for both
// signals: validation failure alone does not certify that GPU writes have ended.
class producer_completion_gate final {
public:
    enum class phase { queue, validation };
    enum class result { pending, success, failure };
private:
    std::array<bool,2> completed_{};
    bool valid_=true;
    result result_=result::pending;
public:
    void reject() noexcept { valid_=false; }
    result state() const noexcept { return result_; }
    bool validated_for_gpu_wait() const noexcept { return completed_[1] && valid_; }
    bool finish(phase source,bool valid) noexcept {
        const auto index=static_cast<size_t>(source);
        if(completed_[index])return false;
        completed_[index]=true;
        valid_ &= valid;
        if(!completed_[0]||!completed_[1])return false;
        result_=valid_ ? result::success : result::failure;
        return true;
    }
};
}
