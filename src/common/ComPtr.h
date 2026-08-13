#pragma once
// RAII wrapper for COM interface pointers (minimal, dependency-free).
#include <unknwn.h>
#include <utility>

namespace ducker::com {

template <typename T>
class Ptr {
public:
    Ptr() noexcept = default;
    Ptr(T* p) noexcept : p_(p) {}
    ~Ptr() { Reset(); }

    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;

    Ptr(Ptr&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    Ptr& operator=(Ptr&& other) noexcept {
        if (this != &other) {
            Reset();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }

    void Reset() {
        if (p_) { p_->Release(); p_ = nullptr; }
    }

    void Attach(T* p) { Reset(); p_ = p; }
    T* Detach() { T* t = p_; p_ = nullptr; return t; }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    T** Put() { Reset(); return &p_; }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

} // namespace ducker::com
