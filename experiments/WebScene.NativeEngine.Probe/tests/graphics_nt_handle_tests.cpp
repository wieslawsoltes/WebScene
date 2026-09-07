#include "graphics/nt_handle.h"
#include "graphics/dxgi_device_identity.h"
#include <set>
using namespace webscene::graphics;
struct test_ops {
    using handle_type=int;
    static inline std::set<int> live;
    static inline int next=1,closed=0;
    static inline bool fail=false;
    static int empty() noexcept { return 0; }
    static bool valid(int value) noexcept { return value>0; }
    static int create() { live.insert(next); return next++; }
    static int duplicate(int source) {
        if (fail || !live.contains(source)) throw std::runtime_error("duplicate failed");
        return create();
    }
    static void close(int value) noexcept {
        if (live.erase(value)!=1) std::terminate();
        ++closed;
    }
};
int main() {
    auto require=[](bool value) { if (!value) throw std::runtime_error("NT handle ownership requirement failed"); };
    using owned=unique_nt_handle<test_ops>;
    const auto borrowed=test_ops::create();
    {
        auto copy=owned::duplicate(borrowed);
        require(copy.get()!=borrowed && test_ops::live.contains(borrowed));
        auto moved=std::move(copy); require(!copy && moved);
        auto replacement=owned::adopt(test_ops::create());
        replacement=std::move(moved); require(!moved && test_ops::closed==1);
        replacement.reset(); replacement.reset(); require(test_ops::closed==2);
        test_ops::fail=true;
        bool failed=false;
        try { auto unexpected=owned::duplicate(borrowed); }
        catch (const std::runtime_error&) { failed=true; }
        require(failed && test_ops::live.size()==1);
        test_ops::fail=false;
        auto transferred=owned::duplicate(borrowed);
        const auto raw=transferred.release(); require(!transferred); test_ops::close(raw);
    }
    require(test_ops::live.contains(borrowed) && test_ops::live.size()==1);
    test_ops::close(borrowed); require(test_ops::live.empty());
#if defined(_WIN32)
    adapter_luid identity{1,2,true};
    require(query_adapter_luid(static_cast<ID3D11Device*>(nullptr),identity)==E_INVALIDARG && !identity.valid);
    identity={1,2,true};
    require(query_adapter_luid(static_cast<ID3D12Device*>(nullptr),identity)==E_INVALIDARG && !identity.valid);
    dxgi_endpoint endpoint{{1,2,true},dxgi_api::d3d11,31,7,true,true};
    require(identify_dxgi_endpoint(static_cast<ID3D12Device*>(nullptr),endpoint)==E_INVALIDARG
        && !endpoint.adapter.valid && endpoint.api==dxgi_api::d3d12 && !endpoint.color_formats
        && !endpoint.alpha_modes && !endpoint.shared_fence && !endpoint.keyed_mutex);
    auto original=owned_nt_handle::adopt(::CreateEventW(nullptr,TRUE,FALSE,nullptr));
    auto duplicate=owned_nt_handle::duplicate(original.get());
    original.reset();
    require(::SetEvent(duplicate.get())!=0 && ::WaitForSingleObject(duplicate.get(),0)==WAIT_OBJECT_0);
    duplicate.reset();
#endif
}
