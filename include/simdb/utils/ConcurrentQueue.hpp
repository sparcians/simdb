// <ConcurrentQueue.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include <deque>
#include <functional>
#include <mutex>

namespace simdb {

/*!
 * \class ConcurrentQueue<T>
 *
 * \brief Thread-safe wrapper around std::queue
 */
template <typename T> class ConcurrentQueue
{
public:
    /// \brief Construct.
    ConcurrentQueue()
    {
        // Get size() code coverage
        simdb_assert(size() == 0);
    }

    /// \brief Push an item to the back of the queue.
    void push(const T& item)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        queue_.push_back(item);
    }

    /// \brief Push an item to the back of the queue (move version).
    void emplace(T&& item)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        queue_.emplace_back(std::move(item));
    }

    /// \brief Construct an item on the back of the queue.
    ///
    /// \param args Forwarding arguments for the <T> constructor.
    template <typename... Args> void emplace(Args&&... args)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        queue_.emplace_back(std::forward<Args>(args)...);
    }

    /// \brief Get the item at the front of the queue.
    ///
    /// \param item Output argument for the popped item.
    ///
    /// \return Returns true if successful, or false if there
    ///         was no data in the queue.
    bool try_pop(T& item)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (queue_.empty())
        {
            return false;
        }
        std::swap(item, queue_.front());
        queue_.pop_front();
        return true;
    }

    /// \brief Get the number of items in this queue.
    size_t size() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return queue_.size();
    }

    /// \brief Check for empty
    bool empty() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return queue_.empty();
    }

    /// \brief Invoke a callback function to peek into this queue's
    /// items. This will be invoked until the callback returns TRUE
    /// or until we have iterated over all queue items.
    bool snoop(const std::function<bool(const T& queue_item)>& cb) const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto& item : queue_)
        {
            if (cb(item))
            {
                return true;
            }
        }
        return false;
    }

private:
    /// Mutex for thread safety.
    mutable std::mutex mutex_;

    /// FIFO queue to hold the data. A deque is used to support snooping.
    std::deque<T> queue_;
};

} // namespace simdb
