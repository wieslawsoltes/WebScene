#pragma once
#include <stdexcept>
#include <utility>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <system_error>
#endif

namespace webscene::graphics {
// Ops supplies the native duplication/close primitives; the same ownership
// implementation can be exercised without requiring a Windows GPU runner.
template<class Ops> class unique_nt_handle {
public:
    using handle_type=typename Ops::handle_type;
private:
    handle_type value_=Ops::empty();
    explicit unique_nt_handle(handle_type owned):value_(owned) {}
public:
    unique_nt_handle()=default;
    unique_nt_handle(const unique_nt_handle&)=delete;
    unique_nt_handle& operator=(const unique_nt_handle&)=delete;
    unique_nt_handle(unique_nt_handle&& other) noexcept :value_(other.release()) {}
    unique_nt_handle& operator=(unique_nt_handle&& other) noexcept {
        if (this!=&other) { reset(); value_=other.release(); }
        return *this;
    }
    ~unique_nt_handle() { reset(); }
    static unique_nt_handle adopt(handle_type owned) {
        if (!Ops::valid(owned)) throw std::invalid_argument("invalid owned NT handle");
        return unique_nt_handle(owned);
    }
    static unique_nt_handle duplicate(handle_type borrowed) {
        if (!Ops::valid(borrowed)) throw std::invalid_argument("invalid borrowed NT handle");
        return adopt(Ops::duplicate(borrowed));
    }
    handle_type get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return Ops::valid(value_); }
    handle_type release() noexcept { return std::exchange(value_,Ops::empty()); }
    void reset() noexcept {
        const auto owned=release();
        if (Ops::valid(owned)) Ops::close(owned);
    }
};
#if defined(_WIN32)
struct win32_nt_handle_ops {
    using handle_type=HANDLE;
    static HANDLE empty() noexcept { return nullptr; }
    static bool valid(HANDLE value) noexcept { return value && value!=INVALID_HANDLE_VALUE; }
    static void close(HANDLE value) noexcept { ::CloseHandle(value); }
    static HANDLE duplicate(HANDLE borrowed) {
        HANDLE result=nullptr;
        if (!::DuplicateHandle(::GetCurrentProcess(),borrowed,::GetCurrentProcess(),&result,
            0,FALSE,DUPLICATE_SAME_ACCESS))
            throw std::system_error(static_cast<int>(::GetLastError()),std::system_category(),"DuplicateHandle");
        return result;
    }
};
// NT handles only; legacy IDXGIResource::GetSharedHandle values are not owned
// CloseHandle-compatible handles and must never be passed to this wrapper.
using owned_nt_handle=unique_nt_handle<win32_nt_handle_ops>;
#endif
} // namespace webscene::graphics
