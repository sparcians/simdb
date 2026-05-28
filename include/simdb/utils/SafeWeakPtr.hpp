// <SafeWeakPtr.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"

#include <memory>
#include <utility>

namespace simdb {

template <typename T> class safe_weak_ptr
{
public:
    safe_weak_ptr() = default;

    safe_weak_ptr(const std::weak_ptr<T>& weak) :
        weak_(weak)
    {
    }

    safe_weak_ptr(std::weak_ptr<T>&& weak) :
        weak_(std::move(weak))
    {
    }

    safe_weak_ptr(const std::shared_ptr<T>& shared) :
        weak_(shared)
    {
    }

    [[nodiscard]]
    bool expired() const noexcept
    {
        return weak_.expired();
    }

    [[nodiscard]]
    std::shared_ptr<T> lock() const
    {
        auto shared = weak_.lock();

        if (!shared)
        {
            throw DBException("safe_weak_ptr: object expired");
        }

        return shared;
    }

    [[nodiscard]]
    std::shared_ptr<T> try_lock() const noexcept
    {
        return weak_.lock();
    }

    T* operator->() const { return lock().get(); }

    T& operator*() const { return *lock(); }

    T* get() const { return lock().get(); }

    explicit operator bool() const noexcept { return !weak_.expired(); }

private:
    std::weak_ptr<T> weak_;
};

} // namespace simdb
