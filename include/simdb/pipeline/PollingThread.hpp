// <PollingThread.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/pipeline/Runnable.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace simdb::pipeline {

class PollingThreadPool;

/// \brief Point-in-time metrics exported by a PollingThread for pool load balancing.
struct PollingThreadMetrics
{
    size_t num_runnables = 0;
    uint64_t num_poll_cycles_with_work = 0;
    double total_sleep_seconds = 0.0;
    double elapsed_seconds = 0.0;
    bool is_running = false;
    bool is_paused = false;
};

/// \brief Per-Runnable PROCEED/SLEEP poll counts since the last pool rebalance reset.
struct RunnablePollMetrics
{
    uint64_t proceed_count = 0;
    uint64_t sleep_count = 0;
};

/*!
 * \class PollingThread
 *
 * \brief Thread that repeatedly polls its Runnables for work; when none do
 *        work, it sleeps for a fixed interval before polling again. Supports
 *        pause/resume. Base for DatabaseThread.
 */
class PollingThread
{
public:
    /// \brief Create a thread with the given sleep interval when no work is done.
    /// \param interval_milliseconds How long to sleep when no Runnable had work (default 100ms).
    PollingThread(const size_t interval_milliseconds = 100) :
        interval_ms_(interval_milliseconds)
    {
    }

    virtual ~PollingThread() noexcept = default;

    /// \brief Return the sleep interval in milliseconds (when no work is done).
    size_t getIntervalMilliseconds() const { return interval_ms_; }

    /// \brief Add a Runnable to this thread; must not be called while the thread is running.
    /// \throws DBException if called while the thread is running.
    void addRunnable(Runnable* runnable)
    {
        if (is_running_)
        {
            throw DBException("Cannot add runnables while thread is running");
        }
        runnables_.emplace_back(runnable);
    }

    /// \brief Return the Runnables on this thread.
    const std::vector<Runnable*>& getRunnables() const { return runnables_; }

    /// \brief Return the number of Runnables on this thread.
    size_t getNumRunnables() const { return runnables_.size(); }

    /// \brief Export metrics for PollingThreadPool load balancing.
    PollingThreadMetrics getMetrics() const noexcept
    {
        PollingThreadMetrics metrics;
        metrics.num_runnables = runnables_.size();
        metrics.num_poll_cycles_with_work = num_times_run_;
        metrics.total_sleep_seconds = total_sleep_seconds_;
        metrics.is_running = is_running_;
        metrics.is_paused = paused_;
        if (is_running_)
        {
            auto now = std::chrono::high_resolution_clock::now();
            metrics.elapsed_seconds = std::chrono::duration<double>(now - start_).count();
        }
        return metrics;
    }

    /// \brief Return PROCEED/SLEEP poll counts for \p runnable since the last rebalance reset.
    RunnablePollMetrics getRunnablePollMetrics(Runnable* runnable) const
    {
        const auto it = runnable_poll_metrics_.find(runnable);
        if (it == runnable_poll_metrics_.end())
        {
            return {};
        }
        return it->second;
    }

    /// \brief Sum of PROCEED poll counts across all runnables on this thread.
    uint64_t getTotalProceedPolls() const
    {
        uint64_t total = 0;
        for (const auto& [_, metrics] : runnable_poll_metrics_)
        {
            total += metrics.proceed_count;
        }
        return total;
    }

    /// \brief Sum of SLEEP poll counts across all runnables on this thread.
    uint64_t getTotalSleepPolls() const
    {
        uint64_t total = 0;
        for (const auto& [_, metrics] : runnable_poll_metrics_)
        {
            total += metrics.sleep_count;
        }
        return total;
    }

    /// \brief Clear per-runnable poll counters (called by PollingThreadPool after each rebalance).
    void resetPollMetrics() { runnable_poll_metrics_.clear(); }

    /// \brief Add a Runnable while this thread is paused (used during pool migration).
    /// \pre paused() == true
    /// \throws DBException if the thread is not paused.
    void addRunnableWhilePaused(Runnable* runnable)
    {
        if (!paused_)
        {
            throw DBException("addRunnableWhilePaused() requires a paused PollingThread");
        }
        runnables_.emplace_back(runnable);
    }

    /// \brief Remove a Runnable while this thread is paused (used during pool migration).
    /// \pre paused() == true
    /// \return true if \p runnable was found and removed.
    /// \throws DBException if the thread is not paused.
    bool removeRunnableWhilePaused(Runnable* runnable)
    {
        if (!paused_)
        {
            throw DBException("removeRunnableWhilePaused() requires a paused PollingThread");
        }
        auto it = std::find(runnables_.begin(), runnables_.end(), runnable);
        if (it == runnables_.end())
        {
            return false;
        }
        runnables_.erase(it);
        runnable_poll_metrics_.erase(runnable);
        return true;
    }

    /// \brief Reorder this thread's Runnables to match the order in \p runnables (only those
    /// that belong to this thread).
    void ensureRelativeOrder(const std::vector<Runnable*>& runnables)
    {
        const std::set<Runnable*> my_runnables(runnables_.begin(), runnables_.end());
        std::vector<Runnable*> ordered_runnables;
        for (auto runnable : runnables)
        {
            if (my_runnables.count(runnable))
            {
                ordered_runnables.push_back(runnable);
            }
        }
        std::swap(ordered_runnables, runnables_);
    }

    /// \brief Call processAll(true) on all enabled Runnables; return true if any did work.
    virtual bool flushRunnables()
    {
        bool did_work = false;
        while (true)
        {
            bool processed = false;
            for (auto runner : runnables_)
            {
                if (!runner->enabled())
                {
                    continue;
                }

                const auto action = runner->processOne(true);
                auto& poll_metrics = runnable_poll_metrics_[runner];
                if (action == PipelineAction::PROCEED)
                {
                    processed = true;
                    ++poll_metrics.proceed_count;
                } else
                {
                    ++poll_metrics.sleep_count;
                }
            }
            if (!processed)
            {
                break;
            }
            did_work = true;
        }
        return did_work;
    }

    /// \brief Start the polling thread.
    virtual void open()
    {
        if (!thread_)
        {
            stop_requested_ = false;
            paused_ = false;
            is_running_ = true;
            start_ = std::chrono::high_resolution_clock::now();
            thread_ = std::make_unique<std::thread>(&PollingThread::loop_, this);
        }
    }

    /// \brief Stop the thread and join.
    /// \note Meant to be called from the main thread.
    virtual void close() noexcept
    {
        if (!thread_)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            stop_requested_ = true;
            paused_ = false; // Unpause in case it was paused
        }
        pause_cv_.notify_all();

        if (thread_->joinable())
        {
            thread_->join();
        }
        is_running_ = false;
        thread_.reset();
    }

    /// \brief Pause the polling loop; blocks until the thread has acknowledged it is paused.
    void pause()
    {
        if (!is_running_ || paused_)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            paused_ = true;

            // Reset and prepare for acknowledgment
            paused_promise_ = std::promise<void>();
            has_pending_pause_ack_ = true;
        }

        pause_cv_.notify_all(); // Wake the thread so it can acknowledge

        // Wait for thread to acknowledge it is paused
        paused_promise_.get_future().wait();
    }

    /// \brief Return true if the thread is currently paused.
    bool paused() { return paused_; }

    /// \brief Resume the polling loop after a pause.
    void resume()
    {
        if (!is_running_ || !paused_)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            paused_ = false;
        }

        pause_cv_.notify_all();
    }

private:
    void loop_()
    {
        while (!stop_requested_)
        {
            // Pause handling
            {
                std::unique_lock<std::mutex> lock(pause_mutex_);
                while (paused_ && !stop_requested_)
                {
                    if (has_pending_pause_ack_)
                    {
                        paused_promise_.set_value();
                        has_pending_pause_ack_ = false;
                    }
                    pause_cv_.wait(lock);
                }

                if (stop_requested_)
                {
                    break;
                }
            }

            if (!run_(false))
            {
                // Sleep for a fixed amount of time before polling all runnables
                // again but wake early if paused or stop is requested
                auto sleep_start = std::chrono::high_resolution_clock::now();
                std::unique_lock<std::mutex> lock(pause_mutex_);
                pause_cv_.wait_for(lock, std::chrono::milliseconds(interval_ms_),
                                   [this] { return paused_ || stop_requested_; });

                auto sleep_end = std::chrono::high_resolution_clock::now();
                auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(sleep_end - sleep_start);
                total_sleep_seconds_ += duration_us.count() / 1'000'000.0;
            } else
            {
                ++num_times_run_;
            }
        }

        // Flush
        while (run_(true))
        {
        }
    }

    virtual bool run_(bool force)
    {
        bool did_work = false;
        while (true)
        {
            bool processed = false;
            for (auto runner : runnables_)
            {
                if (!runner->enabled())
                {
                    continue;
                }

                const auto action = runner->processOne(force);
                auto& poll_metrics = runnable_poll_metrics_[runner];
                if (action == PipelineAction::PROCEED)
                {
                    processed = true;
                    ++poll_metrics.proceed_count;
                } else
                {
                    ++poll_metrics.sleep_count;
                }
            }
            if (!processed)
            {
                break;
            }
            did_work = true;
        }
        return did_work;
    }

    const size_t interval_ms_;
    std::vector<Runnable*> runnables_;
    std::unique_ptr<std::thread> thread_;

    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;

    std::atomic<bool> is_running_ = false;
    std::atomic<bool> paused_ = false;
    std::atomic<bool> stop_requested_ = false;

    std::promise<void> paused_promise_;
    bool has_pending_pause_ack_ = false;

    std::chrono::high_resolution_clock::time_point start_;
    uint64_t num_times_run_ = 0;
    double total_sleep_seconds_ = 0;
    std::unordered_map<Runnable*, RunnablePollMetrics> runnable_poll_metrics_;

    friend class PollingThreadPool;
};

/// Defined here so we can avoid circular includes
inline ScopedRunnableDisabler::ScopedRunnableDisabler(PipelineManager* pipeline_mgr,
                                                      const std::vector<Runnable*>& runnables,
                                                      const std::vector<PollingThread*>& polling_threads) :
    pipeline_mgr_(pipeline_mgr)
{
    // Disable runnables
    for (auto r : runnables)
    {
        if (r->enabled())
        {
            r->enable(false);
            disabled_runnables_.push_back(r);
        }
    }

    // Pause polling threads
    for (auto pt : polling_threads)
    {
        if (!pt->paused())
        {
            pt->pause();
            paused_threads_.push_back(pt);
        }
    }
}

/// Defined here so we can avoid circular includes
inline ScopedRunnableDisabler::ScopedRunnableDisabler(PipelineManager* pipeline_mgr,
                                                      const std::vector<Runnable*>& runnables) :
    pipeline_mgr_(pipeline_mgr)
{
    // Disable runnables
    for (auto r : runnables)
    {
        if (r->enabled())
        {
            r->enable(false);
            disabled_runnables_.push_back(r);
        }
    }
}

/// Defined here so we can avoid circular includes
inline ScopedRunnableDisabler::~ScopedRunnableDisabler()
{
    for (auto pt : paused_threads_)
    {
        pt->resume();
    }

    for (auto r : disabled_runnables_)
    {
        r->enable(true);
    }

    notifyPipelineMgrReenabled_();
}

} // namespace simdb::pipeline
