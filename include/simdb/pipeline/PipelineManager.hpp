// <PipelineManager.hpp> -*- C++ -*-

#pragma once

#include "simdb/pipeline/DatabaseThread.hpp"
#include "simdb/pipeline/Pipeline.hpp"
#include "simdb/pipeline/PipelineSnooper.hpp"
#include "simdb/pipeline/PollingThreadPool.hpp"

#include <iostream>

namespace simdb {
class App;
}

namespace simdb::pipeline {

/*!
 * \class PipelineManager
 *
 * \brief Manages all Pipeline instances, a PollingThreadPool for non-database
 *        stages, and a dedicated DatabaseThread for database stages. Creates
 *        pipelines, opens threads, and provides async DB access.
 */
class PipelineManager
{
public:
    /// \brief Construct with the DatabaseManager to be used by the pipelines.
    /// \param db_mgr Non-null DatabaseManager to be used by the pipelines.
    PipelineManager(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
    }

    /// \brief Return the AsyncDatabaseAccessor for async DB work; only valid after openPipelines().
    /// \throws DBException if called before openPipelines().
    AsyncDatabaseAccessor* getAsyncDatabaseAccessor()
    {
        checkOpen_();
        if (!threads_opened_)
        {
            throw DBException("Cannot access the AsyncDatabaseAccessor before "
                              "calling openPipelines()");
        }
        return async_db_accessor_;
    }

    /// \brief Create and own a new Pipeline with the given name and owning App.
    /// \param name Pipeline name. Only used for reporting purposes.
    /// \param app The App that owns this pipeline.
    /// \return Raw pointer to the new Pipeline (manager retains ownership).
    Pipeline* createPipeline(const std::string& name, const App* app)
    {
        checkOpen_();
        auto pipeline = std::make_unique<Pipeline>(db_mgr_, name, app);
        pipelines_.emplace_back(std::move(pipeline));
        return pipelines_.back().get();
    }

    /// \brief Return pointers to all created pipelines.
    std::vector<Pipeline*> getPipelines()
    {
        checkOpen_();

        std::vector<Pipeline*> pipelines;
        for (auto& pipeline : pipelines_)
        {
            pipelines.push_back(pipeline.get());
        }
        return pipelines;
    }

    /// \brief Create a snooper for iterating stages with a key and snooped object type.
    /// \return Unique_ptr to a PipelineSnooper<KeyType, SnoopedType>.
    template <typename KeyType, typename SnoopedType>
    std::unique_ptr<PipelineSnooper<KeyType, SnoopedType>> createSnooper()
    {
        return std::make_unique<PipelineSnooper<KeyType, SnoopedType>>(this);
    }

    /// \brief Register stages with the thread pool and open all polling threads.
    void openPipelines()
    {
        checkOpen_();

        size_t global_order = 0;
        for (auto& pipeline : pipelines_)
        {
            pipeline->assignStageThreads(thread_pool_, database_thread_, global_order);
        }

        if (database_thread_)
        {
            async_db_accessor_ = database_thread_->getAsyncDatabaseAccessor();
        }

        if (async_db_accessor_)
        {
            for (auto runnable : thread_pool_.getRegisteredRunnables())
            {
                if (auto stage = dynamic_cast<Stage*>(runnable))
                {
                    stage->setAsyncDatabaseAccessor_(async_db_accessor_);
                }
            }
        }

        thread_pool_.open();
        if (database_thread_)
        {
            database_thread_->open();
        }
        threads_opened_ = true;
    }

    /// \brief Temporarily disable all pipeline runnables (and optionally pause threads);
    /// re-enabled when the returnedobject is destroyed.
    /// \param disable_threads_too If true, also pause polling threads; if false, only
    /// disable runnables.
    /// \return A ScopedRunnableDisabler, or nullptr if a disabler is already active (nested
    /// calls are no-ops).
    std::unique_ptr<ScopedRunnableDisabler> scopedDisableAll(bool disable_threads_too = true)
    {
        if (disabler_active_)
        {
            return nullptr;
        }

        getDisablerRunnables_();
        getDisablerThreads_();

        std::unique_ptr<ScopedRunnableDisabler> disabler;
        if (disable_threads_too)
        {
            disabler.reset(new ScopedRunnableDisabler(this, disabler_runnables_, disabler_threads_));
        } else
        {
            disabler.reset(new ScopedRunnableDisabler(this, disabler_runnables_));
        }

        disabler_active_ = true;
        return disabler;
    }

    /// \brief Close all threads, flush runnables, and print the pool performance report.
    void postSimLoopTeardown()
    {
        checkOpen_();

        const auto managed_threads = thread_pool_.getAllManagedThreads(database_thread_.get());

        thread_pool_.close();
        if (database_thread_)
        {
            database_thread_->close();
        }

        bool continue_while;
        do
        {
            continue_while = false;

            for (auto* thread : managed_threads)
            {
                continue_while |= thread->flushRunnables();
            }
        } while (continue_while);

        thread_pool_.printPerfReport();

        closed_ = true;
    }

private:
    /// Associated DatabaseManager.
    DatabaseManager* db_mgr_ = nullptr;

    /// Instantiated pipelines.
    std::vector<std::unique_ptr<Pipeline>> pipelines_;

    /// Pool of worker threads for non-database stages.
    PollingThreadPool thread_pool_;

    /// Dedicated database thread (never part of the pool).
    std::unique_ptr<DatabaseThread> database_thread_;

    /// Threads that we give to the ScopedRunnableDisabler.
    std::vector<PollingThread*> disabler_threads_;

    /// Runnables that we give to the ScopedRunnableDisabler.
    std::vector<Runnable*> disabler_runnables_;

    /// Flag saying whether a ScopedRunnableDisabler is active.
    /// Used in order to short-circuit nested disablers.
    bool disabler_active_ = false;

    /// Flag used to prevent AsyncDatabaseAccessor from being
    /// accessed until threads are opened/finalized.
    bool threads_opened_ = false;

    /// Cached AsyncDatabaseAccessor for async DB queries.
    AsyncDatabaseAccessor* async_db_accessor_ = nullptr;

    void getDisablerThreads_()
    {
        if (!disabler_threads_.empty())
        {
            return;
        }

        disabler_threads_ = thread_pool_.getAllManagedThreads(database_thread_.get());

        // Ensure unique
        auto it = std::unique(disabler_threads_.begin(), disabler_threads_.end());
        if (it != disabler_threads_.end())
        {
            throw DBException("Internal error: duplicate threads found in disabler_threads_");
        }
    }

    void getDisablerRunnables_()
    {
        if (!disabler_runnables_.empty())
        {
            return;
        }

        for (auto runnable : thread_pool_.getRegisteredRunnables())
        {
            disabler_runnables_.push_back(runnable);
        }
        if (database_thread_)
        {
            const auto& db_runnables = database_thread_->getRunnables();
            disabler_runnables_.insert(disabler_runnables_.end(), db_runnables.begin(), db_runnables.end());
        }

        // Ensure unique
        auto it = std::unique(disabler_runnables_.begin(), disabler_runnables_.end());
        if (it != disabler_runnables_.end())
        {
            throw DBException("Internal error: duplicate runnables found in "
                              "disabler_runnables_");
        }
    }

    /// Get a notification when a disabler goes out of scope.
    friend class ScopedRunnableDisabler;
    void onDisablerDestruction_()
    {
        if (!disabler_active_)
        {
            throw DBException("Internal error: no disabler active in "
                              "onDisablerDestruction_()");
        }
        disabler_active_ = false;
    }

    /// Flag saying whether postSimLoopTeardown() was called.
    bool closed_ = false;

    /// Validate that no APIs are called after closing the pipelines
    void checkOpen_() const
    {
        if (closed_)
        {
            throw DBException("PipelineManager has been closed");
        }
    }
};

/// Defined here so we can avoid circular includes
inline void ScopedRunnableDisabler::notifyPipelineMgrReenabled_()
{
    pipeline_mgr_->onDisablerDestruction_();
}

/// Defined here so we can avoid circular includes
template <typename KeyType, typename SnoopedType>
bool PipelineSnooper<KeyType, SnoopedType>::snoopAllStages(const KeyType& key, SnoopedType& snooped_obj,
                                                           bool disable_pipeline)
{
    std::unique_ptr<ScopedRunnableDisabler> disabler = disable_pipeline ? pipeline_mgr_->scopedDisableAll() : nullptr;

    for (auto& cb : callbacks_)
    {
        if (cb(key, snooped_obj))
        {
            return true;
        }
    }
    return false;
}

} // namespace simdb::pipeline
