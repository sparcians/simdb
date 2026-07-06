// <PollingThreadPool.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/pipeline/DatabaseThread.hpp"
#include "simdb/pipeline/PollingThread.hpp"
#include "simdb/pipeline/Runnable.hpp"
#include "simdb/utils/StreamFormatters.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace simdb::pipeline {

/// \brief Rolling load snapshot for one pool worker thread.
struct ThreadLoadSnapshot
{
    PollingThread* thread = nullptr;
    size_t num_runnables = 0;
    uint64_t num_poll_cycles_with_work = 0;
    double pct_time_sleeping = 0.0;
    double pct_time_working = 0.0;
    /// Fraction of recent runnable polls that returned PROCEED (0..1).
    double recent_busy_ratio = 0.0;
    bool is_running = false;
    bool is_paused = false;
};

/// \brief Metadata for a non-database Runnable registered with the pool.
struct RunnableRegistration
{
    Runnable* runnable = nullptr;
    size_t interval_ms = 100;
    /// Global pipeline definition order; used to preserve relative ordering
    /// within a worker's runnable list (replaces ensureRelativeOrder()).
    size_t global_order = 0;
    PollingThread* assigned_thread = nullptr;
    uint64_t migration_count = 0;
    uint64_t lifetime_proceed_count = 0;
    uint64_t lifetime_sleep_count = 0;
};

/*!
 * \class PollingThreadPool
 *
 * \brief Owns a dynamic set of PollingThread workers and automatically
 *        balances non-database Runnables across them. Grows and shrinks the
 *        worker count based on observed load. DatabaseThread and its runnables
 *        never participate in this pool.
 *
 * Typical lifecycle (via PipelineManager):
 *   1. registerRunnable() for each non-DB stage (before open)
 *   2. open() — create initial workers, distribute runnables, start balancer
 *   3. runtime — balancer steals/migrates runnables and resizes pool
 *   4. close() — stop balancer, drain workers, join all threads
 */
class PollingThreadPool
{
public:
    /// \brief Tunables for pool sizing and steal decisions.
    struct Config
    {
        /// Minimum number of worker PollingThreads kept open while the pool is running.
        size_t min_threads = 1;
        /// Maximum number of worker PollingThreads the pool may create.
        size_t max_threads = 0; // 0 => std::thread::hardware_concurrency()
        /// Default sleep interval for newly created workers (ms).
        size_t default_interval_ms = 100;
        /// How often the balancer re-evaluates load and may steal or resize.
        std::chrono::milliseconds rebalance_period{500};
        /// Steal source: thread recent_busy_ratio must exceed this (0..1).
        double steal_busy_threshold = 0.70;
        /// Steal destination: thread recent_busy_ratio must be below this (0..1).
        double steal_idle_threshold = 0.30;
        /// Grow pool when every worker exceeds this busy ratio (0..1).
        double grow_all_busy_threshold = 0.80;
        /// Shrink candidate: worker busy ratio below this and runnable count <= shrink_max_runnables.
        double shrink_idle_threshold = 0.10;
        /// Max runnables on a thread eligible for shrink (typically 0 or 1).
        size_t shrink_max_runnables = 0;
    };

    explicit PollingThreadPool(Config config) :
        config_(std::move(config))
    {
        if (config_.max_threads == 0)
        {
            config_.max_threads = std::max(size_t{1}, static_cast<size_t>(std::thread::hardware_concurrency()));
        }
        config_.min_threads = std::max(size_t{1}, std::min(config_.min_threads, config_.max_threads));
    }

    PollingThreadPool() :
        PollingThreadPool(Config{})
    {
    }

    /// \brief Register a pool-eligible Runnable before open().
    /// \param runnable Non-null stage or other non-DB runnable.
    /// \param interval_ms Polling sleep interval when this runnable has no work.
    /// \param global_order Position in global pipeline stage definition order.
    /// \throws DBException if called after open() or if runnable is already registered.
    void registerRunnable(Runnable* runnable, size_t interval_ms, size_t global_order)
    {
        if (is_open_)
        {
            throw DBException("Cannot register runnables after PollingThreadPool::open()");
        }
        if (!runnable)
        {
            throw DBException("PollingThreadPool::registerRunnable() requires non-null runnable");
        }
        for (const auto& reg : registrations_)
        {
            if (reg.runnable == runnable)
            {
                throw DBException("Runnable already registered with PollingThreadPool");
            }
        }
        registrations_.push_back({runnable, interval_ms, global_order, nullptr});
    }

    /// \brief Create workers, assign runnables, and start the balancer thread.
    /// \throws DBException if already open.
    void open();

    /// \brief Stop the balancer, close all workers, and join threads.
    void close() noexcept;

    /// \brief Return true after open() and before close().
    bool isOpen() const { return is_open_; }

    /// \brief Return all worker threads (for pause/disable integration).
    std::vector<PollingThread*> getWorkerThreads() const;

    /// \brief Return all threads the pool manager should treat as polling threads:
    ///        worker threads plus an optional dedicated DatabaseThread.
    std::vector<PollingThread*> getAllManagedThreads(DatabaseThread* database_thread = nullptr) const;

    /// \brief Return registered runnables in global definition order.
    std::vector<Runnable*> getRegisteredRunnables() const;

    /// \brief Return the pool configuration.
    const Config& getConfig() const { return config_; }

    /// \brief Force an immediate rebalance (primarily for tests).
    void rebalanceNow();

    /// \brief Print end-of-sim pool summary and final worker layout.
    /// \note Call before close() so worker utilization metrics are still available.
    void printPerfReport(std::ostream& os = std::cout);

private:
    struct IntervalWorkerGroup
    {
        size_t interval_ms = 100;
        std::vector<std::unique_ptr<PollingThread>> workers;
    };

    void rebalanceLoop_();
    void rebalanceOnce_();
    void createInitialWorkers_();
    void distributeInitialRunnables_();
    void ensureRelativeOrderOnThread_(PollingThread* thread);
    ThreadLoadSnapshot snapshotThread_(PollingThread* thread) const;
    std::vector<ThreadLoadSnapshot> snapshotAllWorkers_() const;
    IntervalWorkerGroup* findWorkerGroupForInterval_(size_t interval_ms);
    const IntervalWorkerGroup* findWorkerGroupForThread_(PollingThread* thread) const;
    size_t totalWorkerCount_() const;

    PollingThread* findStealDestination_(const ThreadLoadSnapshot& source,
                                         const std::vector<ThreadLoadSnapshot>& snapshots) const;
    Runnable* findStealCandidate_(PollingThread* source_thread) const;

    void migrateRunnable_(Runnable* runnable, PollingThread* from, PollingThread* to);
    void growPool_(size_t interval_ms);
    void shrinkPool_(const std::vector<ThreadLoadSnapshot>& snapshots);
    void accumulatePollMetricsFromWorkers_();
    void resetWorkerPollMetrics_();
    double runnableProceedPct_(const RunnableRegistration& reg) const;
    double workerProceedPct_(PollingThread* worker) const;
    void updatePeakWorkerCount_();
    std::string formatWorkerLabel_(PollingThread* thread) const;
    std::unique_ptr<PollingThread> createWorker_(size_t interval_ms);

    Config config_;
    std::vector<RunnableRegistration> registrations_;
    std::vector<IntervalWorkerGroup> worker_groups_;

    size_t initial_worker_count_ = 0;
    size_t peak_worker_count_ = 0;
    uint64_t num_rebalances_ = 0;
    uint64_t num_steals_ = 0;
    uint64_t num_grows_ = 0;
    uint64_t num_shrinks_ = 0;
    uint64_t num_migrations_ = 0;
    uint64_t grow_blocked_at_max_ = 0;

    std::unique_ptr<std::thread> balancer_thread_;
    std::mutex pool_mutex_;
    std::atomic<bool> is_open_{false};
    std::atomic<bool> stop_balancer_{false};
};

inline void PollingThreadPool::open()
{
    if (is_open_)
    {
        throw DBException("PollingThreadPool::open() called more than once");
    }
    if (registrations_.empty())
    {
        throw DBException("PollingThreadPool::open() requires at least one registered runnable");
    }

    std::lock_guard<std::mutex> lock(pool_mutex_);

    createInitialWorkers_();
    distributeInitialRunnables_();
    initial_worker_count_ = totalWorkerCount_();
    peak_worker_count_ = initial_worker_count_;

    for (auto& group : worker_groups_)
    {
        for (auto& worker : group.workers)
        {
            worker->open();
        }
    }

    stop_balancer_ = false;
    balancer_thread_ = std::make_unique<std::thread>(&PollingThreadPool::rebalanceLoop_, this);
    is_open_ = true;
}

inline void PollingThreadPool::close() noexcept
{
    if (!is_open_)
    {
        return;
    }

    stop_balancer_ = true;
    if (balancer_thread_ && balancer_thread_->joinable())
    {
        balancer_thread_->join();
    }
    balancer_thread_.reset();

    for (auto& group : worker_groups_)
    {
        for (auto& worker : group.workers)
        {
            worker->close();
        }
    }

    for (auto& reg : registrations_)
    {
        reg.assigned_thread = nullptr;
    }

    is_open_ = false;
}

inline std::vector<PollingThread*> PollingThreadPool::getWorkerThreads() const
{
    std::vector<PollingThread*> threads;
    for (const auto& group : worker_groups_)
    {
        for (const auto& worker : group.workers)
        {
            threads.push_back(worker.get());
        }
    }
    return threads;
}

inline std::vector<PollingThread*> PollingThreadPool::getAllManagedThreads(DatabaseThread* database_thread) const
{
    auto threads = getWorkerThreads();
    if (database_thread)
    {
        threads.push_back(database_thread);
    }
    return threads;
}

inline std::vector<Runnable*> PollingThreadPool::getRegisteredRunnables() const
{
    std::vector<Runnable*> runnables;
    runnables.reserve(registrations_.size());
    for (const auto& reg : registrations_)
    {
        runnables.push_back(reg.runnable);
    }
    return runnables;
}

inline void PollingThreadPool::rebalanceNow()
{
    if (!is_open_)
    {
        throw DBException("PollingThreadPool::rebalanceNow() requires an open pool");
    }
    rebalanceOnce_();
}

inline void PollingThreadPool::rebalanceLoop_()
{
    while (!stop_balancer_)
    {
        std::this_thread::sleep_for(config_.rebalance_period);
        if (stop_balancer_)
        {
            break;
        }
        rebalanceOnce_();
    }
}

inline void PollingThreadPool::rebalanceOnce_()
{
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (worker_groups_.empty())
    {
        return;
    }

    ++num_rebalances_;

    const auto snapshots = snapshotAllWorkers_();

    // Steal one runnable from an overloaded worker to an idle peer (same interval group).
    for (const auto& source : snapshots)
    {
        if (source.recent_busy_ratio <= config_.steal_busy_threshold)
        {
            continue;
        }
        if (source.num_runnables <= 1)
        {
            continue;
        }

        PollingThread* dest = findStealDestination_(source, snapshots);
        if (!dest)
        {
            continue;
        }

        Runnable* candidate = findStealCandidate_(source.thread);
        if (!candidate)
        {
            continue;
        }

        migrateRunnable_(candidate, source.thread, dest);
        ++num_steals_;
        break;
    }

    // Grow when every worker is busy and the pool is below its cap.
    bool all_busy = !snapshots.empty();
    for (const auto& snap : snapshots)
    {
        if (snap.recent_busy_ratio <= config_.grow_all_busy_threshold)
        {
            all_busy = false;
            break;
        }
    }
    if (all_busy)
    {
        if (totalWorkerCount_() < config_.max_threads)
        {
            size_t grow_interval_ms = worker_groups_.front().interval_ms;
            double busiest_avg = -1.0;
            for (const auto& group : worker_groups_)
            {
                double sum_busy = 0.0;
                size_t count = 0;
                for (const auto& snap : snapshots)
                {
                    const auto* snap_group = findWorkerGroupForThread_(snap.thread);
                    if (snap_group && snap_group->interval_ms == group.interval_ms)
                    {
                        sum_busy += snap.recent_busy_ratio;
                        ++count;
                    }
                }
                if (count == 0)
                {
                    continue;
                }
                const double avg_busy = sum_busy / static_cast<double>(count);
                if (avg_busy > busiest_avg)
                {
                    busiest_avg = avg_busy;
                    grow_interval_ms = group.interval_ms;
                }
            }
            growPool_(grow_interval_ms);
        } else
        {
            ++grow_blocked_at_max_;
        }
    }

    shrinkPool_(snapshotAllWorkers_());
    resetWorkerPollMetrics_();
}

inline void PollingThreadPool::createInitialWorkers_()
{
    std::set<size_t> intervals;
    for (const auto& reg : registrations_)
    {
        intervals.insert(reg.interval_ms);
    }

    const size_t num_groups = std::max(size_t{1}, intervals.size());
    const size_t workers_per_group = std::max(size_t{1}, config_.min_threads / num_groups);

    for (size_t interval_ms : intervals)
    {
        IntervalWorkerGroup group;
        group.interval_ms = interval_ms;
        for (size_t i = 0; i < workers_per_group; ++i)
        {
            group.workers.push_back(createWorker_(interval_ms));
        }
        worker_groups_.push_back(std::move(group));
    }

    while (totalWorkerCount_() < config_.min_threads)
    {
        worker_groups_.front().workers.push_back(createWorker_(worker_groups_.front().interval_ms));
    }
}

inline PollingThreadPool::IntervalWorkerGroup* PollingThreadPool::findWorkerGroupForInterval_(size_t interval_ms)
{
    for (auto& group : worker_groups_)
    {
        if (group.interval_ms == interval_ms)
        {
            return &group;
        }
    }
    return nullptr;
}

inline const PollingThreadPool::IntervalWorkerGroup*
PollingThreadPool::findWorkerGroupForThread_(PollingThread* thread) const
{
    for (const auto& group : worker_groups_)
    {
        for (const auto& worker : group.workers)
        {
            if (worker.get() == thread)
            {
                return &group;
            }
        }
    }
    return nullptr;
}

inline size_t PollingThreadPool::totalWorkerCount_() const
{
    size_t count = 0;
    for (const auto& group : worker_groups_)
    {
        count += group.workers.size();
    }
    return count;
}

inline void PollingThreadPool::distributeInitialRunnables_()
{
    std::map<size_t, size_t> worker_idx_by_interval;
    for (auto& reg : registrations_)
    {
        auto* group = findWorkerGroupForInterval_(reg.interval_ms);
        if (!group || group->workers.empty())
        {
            throw DBException("Internal error: no worker group for runnable interval");
        }

        size_t& worker_idx = worker_idx_by_interval[reg.interval_ms];
        auto& worker = group->workers.at(worker_idx % group->workers.size());
        worker->addRunnable(reg.runnable);
        reg.assigned_thread = worker.get();

        ++worker_idx;
    }

    for (auto& group : worker_groups_)
    {
        for (auto& worker : group.workers)
        {
            ensureRelativeOrderOnThread_(worker.get());
        }
    }
}

inline void PollingThreadPool::ensureRelativeOrderOnThread_(PollingThread* thread)
{
    std::vector<Runnable*> ordered;
    ordered.reserve(registrations_.size());
    for (const auto& reg : registrations_)
    {
        ordered.push_back(reg.runnable);
    }
    thread->ensureRelativeOrder(ordered);
}

inline ThreadLoadSnapshot PollingThreadPool::snapshotThread_(PollingThread* thread) const
{
    ThreadLoadSnapshot snapshot;
    snapshot.thread = thread;
    const auto metrics = thread->getMetrics();
    snapshot.num_runnables = metrics.num_runnables;
    snapshot.num_poll_cycles_with_work = metrics.num_poll_cycles_with_work;
    snapshot.is_running = metrics.is_running;
    snapshot.is_paused = metrics.is_paused;
    if (metrics.elapsed_seconds > 0.0)
    {
        snapshot.pct_time_sleeping = (metrics.total_sleep_seconds / metrics.elapsed_seconds) * 100.0;
        snapshot.pct_time_working = 100.0 - snapshot.pct_time_sleeping;
    }

    const uint64_t proceed_polls = thread->getTotalProceedPolls();
    const uint64_t sleep_polls = thread->getTotalSleepPolls();
    const uint64_t total_polls = proceed_polls + sleep_polls;
    if (total_polls > 0)
    {
        snapshot.recent_busy_ratio = static_cast<double>(proceed_polls) / static_cast<double>(total_polls);
    } else if (metrics.elapsed_seconds > 0.0)
    {
        snapshot.recent_busy_ratio = snapshot.pct_time_working / 100.0;
    }
    return snapshot;
}

inline std::vector<ThreadLoadSnapshot> PollingThreadPool::snapshotAllWorkers_() const
{
    std::vector<ThreadLoadSnapshot> snapshots;
    for (const auto& group : worker_groups_)
    {
        for (const auto& worker : group.workers)
        {
            snapshots.push_back(snapshotThread_(worker.get()));
        }
    }
    return snapshots;
}

inline PollingThread* PollingThreadPool::findStealDestination_(const ThreadLoadSnapshot& source,
                                                               const std::vector<ThreadLoadSnapshot>& snapshots) const
{
    const auto* source_group = findWorkerGroupForThread_(source.thread);
    if (!source_group)
    {
        return nullptr;
    }

    PollingThread* best = nullptr;
    double best_idle = 1.0;
    for (const auto& snap : snapshots)
    {
        if (snap.thread == source.thread)
        {
            continue;
        }
        const auto* dest_group = findWorkerGroupForThread_(snap.thread);
        if (!dest_group || dest_group->interval_ms != source_group->interval_ms)
        {
            continue;
        }
        if (snap.recent_busy_ratio < config_.steal_idle_threshold && snap.recent_busy_ratio < best_idle)
        {
            best_idle = snap.recent_busy_ratio;
            best = snap.thread;
        }
    }
    return best;
}

inline Runnable* PollingThreadPool::findStealCandidate_(PollingThread* source_thread) const
{
    Runnable* best = nullptr;
    double best_busy_ratio = -1.0;
    size_t best_global_order = 0;

    for (const auto& reg : registrations_)
    {
        if (reg.assigned_thread != source_thread)
        {
            continue;
        }

        const auto poll_metrics = source_thread->getRunnablePollMetrics(reg.runnable);
        const uint64_t total_polls = poll_metrics.proceed_count + poll_metrics.sleep_count;
        if (total_polls > 0)
        {
            const double busy_ratio =
                static_cast<double>(poll_metrics.proceed_count) / static_cast<double>(total_polls);
            if (busy_ratio > best_busy_ratio)
            {
                best_busy_ratio = busy_ratio;
                best = reg.runnable;
            }
            continue;
        }

        if (!best || reg.global_order > best_global_order)
        {
            best = reg.runnable;
            best_global_order = reg.global_order;
        }
    }

    return best;
}

inline void PollingThreadPool::migrateRunnable_(Runnable* runnable, PollingThread* from, PollingThread* to)
{
    from->pause();
    if (!from->removeRunnableWhilePaused(runnable))
    {
        from->resume();
        throw DBException("Internal error: failed to remove runnable during migration");
    }
    to->pause();
    to->addRunnableWhilePaused(runnable);
    ensureRelativeOrderOnThread_(to);
    to->resume();
    from->resume();

    for (auto& reg : registrations_)
    {
        if (reg.runnable == runnable)
        {
            reg.assigned_thread = to;
            ++reg.migration_count;
            break;
        }
    }

    ++num_migrations_;
}

inline void PollingThreadPool::growPool_(size_t interval_ms)
{
    if (totalWorkerCount_() >= config_.max_threads)
    {
        return;
    }
    auto* group = findWorkerGroupForInterval_(interval_ms);
    if (!group)
    {
        return;
    }
    auto worker = createWorker_(interval_ms);
    auto* worker_ptr = worker.get();
    group->workers.emplace_back(std::move(worker));
    worker_ptr->openAllowEmpty();
    ++num_grows_;
    updatePeakWorkerCount_();
}

inline void PollingThreadPool::shrinkPool_(const std::vector<ThreadLoadSnapshot>& snapshots)
{
    if (totalWorkerCount_() <= config_.min_threads)
    {
        return;
    }

    for (const auto& snap : snapshots)
    {
        if (snap.recent_busy_ratio >= config_.shrink_idle_threshold)
        {
            continue;
        }
        if (snap.num_runnables > config_.shrink_max_runnables)
        {
            continue;
        }

        PollingThread* thread = snap.thread;
        IntervalWorkerGroup* group = nullptr;
        for (auto& candidate_group : worker_groups_)
        {
            for (const auto& worker : candidate_group.workers)
            {
                if (worker.get() == thread)
                {
                    group = &candidate_group;
                    break;
                }
            }
            if (group)
            {
                break;
            }
        }
        if (!group)
        {
            continue;
        }

        if (snap.num_runnables > 0)
        {
            if (group->workers.size() <= 1)
            {
                continue;
            }

            PollingThread* dest = nullptr;
            double best_idle = 1.0;
            for (const auto& other_snap : snapshots)
            {
                if (other_snap.thread == thread)
                {
                    continue;
                }
                const auto* other_group = findWorkerGroupForThread_(other_snap.thread);
                if (!other_group || other_group->interval_ms != group->interval_ms)
                {
                    continue;
                }
                if (other_snap.recent_busy_ratio < best_idle)
                {
                    best_idle = other_snap.recent_busy_ratio;
                    dest = other_snap.thread;
                }
            }
            if (!dest)
            {
                continue;
            }

            std::vector<Runnable*> to_migrate;
            for (const auto& reg : registrations_)
            {
                if (reg.assigned_thread == thread)
                {
                    to_migrate.push_back(reg.runnable);
                }
            }
            for (Runnable* runnable : to_migrate)
            {
                migrateRunnable_(runnable, thread, dest);
            }
        }

        thread->close();
        for (auto it = group->workers.begin(); it != group->workers.end(); ++it)
        {
            if (it->get() == thread)
            {
                group->workers.erase(it);
                break;
            }
        }
        ++num_shrinks_;
        return;
    }
}

inline void PollingThreadPool::accumulatePollMetricsFromWorkers_()
{
    for (const auto& group : worker_groups_)
    {
        for (const auto& worker : group.workers)
        {
            for (Runnable* runnable : worker->getRunnables())
            {
                const auto window = worker->getRunnablePollMetrics(runnable);
                if (window.proceed_count == 0 && window.sleep_count == 0)
                {
                    continue;
                }

                for (auto& reg : registrations_)
                {
                    if (reg.runnable == runnable)
                    {
                        reg.lifetime_proceed_count += window.proceed_count;
                        reg.lifetime_sleep_count += window.sleep_count;
                        break;
                    }
                }
            }
        }
    }
}

inline void PollingThreadPool::resetWorkerPollMetrics_()
{
    accumulatePollMetricsFromWorkers_();
    for (auto& group : worker_groups_)
    {
        for (auto& worker : group.workers)
        {
            worker->resetPollMetrics();
        }
    }
}

inline void PollingThreadPool::updatePeakWorkerCount_()
{
    peak_worker_count_ = std::max(peak_worker_count_, totalWorkerCount_());
}

inline double PollingThreadPool::runnableProceedPct_(const RunnableRegistration& reg) const
{
    uint64_t proceed = reg.lifetime_proceed_count;
    uint64_t sleep = reg.lifetime_sleep_count;

    PollingThread* worker = reg.assigned_thread;
    if (!worker)
    {
        for (const auto& group : worker_groups_)
        {
            for (const auto& candidate : group.workers)
            {
                const auto& runnables = candidate->getRunnables();
                if (std::find(runnables.begin(), runnables.end(), reg.runnable) != runnables.end())
                {
                    worker = candidate.get();
                    break;
                }
            }
            if (worker)
            {
                break;
            }
        }
    }

    if (worker)
    {
        const auto window = worker->getRunnablePollMetrics(reg.runnable);
        proceed += window.proceed_count;
        sleep += window.sleep_count;
    }

    const uint64_t total = proceed + sleep;
    if (total == 0)
    {
        return -1.0;
    }
    return (static_cast<double>(proceed) / static_cast<double>(total)) * 100.0;
}

inline double PollingThreadPool::workerProceedPct_(PollingThread* worker) const
{
    if (!worker)
    {
        return -1.0;
    }

    uint64_t proceed = 0;
    uint64_t sleep = 0;
    for (Runnable* runnable : worker->getRunnables())
    {
        for (const auto& reg : registrations_)
        {
            if (reg.runnable != runnable)
            {
                continue;
            }
            proceed += reg.lifetime_proceed_count;
            sleep += reg.lifetime_sleep_count;
            break;
        }

        const auto window = worker->getRunnablePollMetrics(runnable);
        proceed += window.proceed_count;
        sleep += window.sleep_count;
    }

    const uint64_t total = proceed + sleep;
    if (total == 0)
    {
        return -1.0;
    }
    return (static_cast<double>(proceed) / static_cast<double>(total)) * 100.0;
}

inline std::string PollingThreadPool::formatWorkerLabel_(PollingThread* thread) const
{
    for (const auto& group : worker_groups_)
    {
        size_t worker_idx = 0;
        for (const auto& worker : group.workers)
        {
            if (worker.get() == thread)
            {
                return "[" + std::to_string(group.interval_ms) + "ms] #" + std::to_string(worker_idx);
            }
            ++worker_idx;
        }
    }
    return "unknown";
}

inline void PollingThreadPool::printPerfReport(std::ostream& os)
{
    std::lock_guard<std::mutex> lock(pool_mutex_);

    if (initial_worker_count_ == 0 && worker_groups_.empty())
    {
        return;
    }

    std::vector<PollingThread*> paused_workers;
    paused_workers.reserve(totalWorkerCount_());
    for (auto& group : worker_groups_)
    {
        for (auto& worker : group.workers)
        {
            auto* worker_ptr = worker.get();
            const auto metrics = worker_ptr->getMetrics();
            if (metrics.is_running && !worker_ptr->paused())
            {
                worker_ptr->pause();
                paused_workers.push_back(worker_ptr);
            }
        }
    }

    const auto resume_workers = [&paused_workers]() {
        for (auto* worker_ptr : paused_workers)
        {
            worker_ptr->resume();
        }
    };

    const size_t final_worker_count = totalWorkerCount_();

    [[maybe_unused]] ios_format_saver fmt_saver(os);
    os << "PollingThreadPool performance report\n\n";

    os << "    Summary:\n";
    os << "        Workers:              " << initial_worker_count_ << " -> peak " << peak_worker_count_ << " -> final "
       << final_worker_count << "  (min=" << config_.min_threads << " max=" << config_.max_threads << ")\n";
    os << "        Registered runnables: " << registrations_.size() << "\n";
    os << "        Rebalance cycles:     " << num_rebalances_ << "\n";
    os << "        Steals:               " << num_steals_ << "\n";
    os << "        Grows:                " << num_grows_ << "\n";
    os << "        Shrinks:              " << num_shrinks_ << "\n";
    os << "        Runnable migrations:  " << num_migrations_ << "\n";
    os << "        Grow blocked (max):   " << grow_blocked_at_max_ << "\n";

    os << "\n    Final worker layout:\n";
    for (const auto& group : worker_groups_)
    {
        size_t worker_idx = 0;
        for (const auto& worker : group.workers)
        {
            os << "        Worker [" << group.interval_ms << "ms] #" << worker_idx << ":  ";

            const auto& runnables = worker->getRunnables();
            if (runnables.empty())
            {
                os << "(empty)";
            } else
            {
                for (size_t i = 0; i < runnables.size(); ++i)
                {
                    if (i > 0)
                    {
                        os << ", ";
                    }
                    os << runnables[i]->getDescription();
                }
            }

            const auto snap = snapshotThread_(worker.get());
            const double worker_proceed_pct = workerProceedPct_(worker.get());
            if (worker_proceed_pct >= 0.0)
            {
                os << "  (" << std::fixed << std::setprecision(1) << worker_proceed_pct << "% proceed polls)\n";
            } else if (snap.pct_time_working > 0.0)
            {
                os << "  (" << std::fixed << std::setprecision(1) << snap.pct_time_working << "% working)\n";
            } else
            {
                os << "  (n/a)\n";
            }
            ++worker_idx;
        }
    }

    std::vector<const RunnableRegistration*> sorted_regs;
    sorted_regs.reserve(registrations_.size());
    for (const auto& reg : registrations_)
    {
        sorted_regs.push_back(&reg);
    }
    std::sort(sorted_regs.begin(), sorted_regs.end(), [](const RunnableRegistration* a, const RunnableRegistration* b) {
        return a->global_order < b->global_order;
    });

    os << "\n    Runnables:\n";
    accumulatePollMetricsFromWorkers_();
    for (auto& group : worker_groups_)
    {
        for (auto& worker : group.workers)
        {
            worker->resetPollMetrics();
        }
    }
    for (const RunnableRegistration* reg : sorted_regs)
    {
        PollingThread* worker = reg->assigned_thread;
        if (!worker)
        {
            for (const auto& group : worker_groups_)
            {
                for (const auto& candidate : group.workers)
                {
                    const auto& runnables = candidate->getRunnables();
                    if (std::find(runnables.begin(), runnables.end(), reg->runnable) != runnables.end())
                    {
                        worker = candidate.get();
                        break;
                    }
                }
                if (worker)
                {
                    break;
                }
            }
        }

        const double proceed_pct = runnableProceedPct_(*reg);
        os << "        " << reg->runnable->getDescription() << ":  ";
        if (proceed_pct >= 0.0)
        {
            os << std::fixed << std::setprecision(1) << proceed_pct << "% proceed polls";
        } else
        {
            os << "n/a proceed polls";
        }

        os << ",  final worker " << formatWorkerLabel_(worker) << ",  migrated " << reg->migration_count << " times\n";
    }

    os << "\n    Configuration:\n";
    os << "        rebalance_period:        " << config_.rebalance_period.count() << "ms\n";
    os << "        steal_busy_threshold:    " << config_.steal_busy_threshold << "\n";
    os << "        steal_idle_threshold:    " << config_.steal_idle_threshold << "\n";
    os << "        grow_all_busy_threshold: " << config_.grow_all_busy_threshold << "\n";
    os << "        shrink_idle_threshold:   " << config_.shrink_idle_threshold << "\n";
    os << "        shrink_max_runnables:    " << config_.shrink_max_runnables << "\n";
    os << "\n";

    resume_workers();
}

inline std::unique_ptr<PollingThread> PollingThreadPool::createWorker_(size_t interval_ms)
{
    return std::make_unique<PollingThread>(interval_ms);
}

} // namespace simdb::pipeline
