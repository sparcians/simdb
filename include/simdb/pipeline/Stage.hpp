// <Stage.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/DatabaseAccessor.hpp"
#include "simdb/pipeline/DatabaseThread.hpp"
#include "simdb/pipeline/PollingThread.hpp"
#include "simdb/pipeline/QueueRepo.hpp"
#include "simdb/pipeline/Runnable.hpp"
#include "simdb/utils/Compress.hpp"
#include <algorithm>
#include <memory>
#include <unordered_map>

namespace simdb::pipeline {

/*!
 * \class Stage
 *
 * \brief Base class for pipeline stages (Runnables with input/output ports
 *        and an optional polling interval). Subclass to implement run_(); use
 *        addInPort_/addOutPort_ in derived constructors to define queues.
 *        The interval is the PollingThread sleep time when no work is done.
 */
class Stage : public Runnable
{
protected:
    /// \brief Construct with the polling interval (ms) for the thread when no work is done.
    /// \param interval_milliseconds Sleep time for the PollingThread; non-database stages
    ///        that share a thread must use the same interval.
    Stage(size_t interval_milliseconds = 100) :
        interval_milliseconds_(interval_milliseconds)
    {
    }

    template <typename T> void addInPort_(const std::string& port_name, ConcurrentQueue<T>*& queue)
    {
        queue_repo_.addInPortPlaceholder<T>(port_name, queue);
    }

    template <typename T> void addOutPort_(const std::string& port_name, ConcurrentQueue<T>*& queue)
    {
        queue_repo_.addOutPortPlaceholder<T>(port_name, queue);
    }

    virtual AsyncDatabaseAccessor* getAsyncDatabaseAccessor_() const { return async_db_accessor_; }

private:
    void setName_(const std::string& name)
    {
        name_ = name;
        queue_repo_.setStageName(name);
    }

    void mergeQueueRepo_(PipelineQueueRepo& master_repo) { master_repo.merge(queue_repo_); }

    virtual void assignThread_(DatabaseManager*, std::vector<std::unique_ptr<PollingThread>>& threads,
                               std::unique_ptr<DatabaseThread>&)
    {
        threads.emplace_back(std::make_unique<PollingThread>(interval_milliseconds_));
        threads.back()->addRunnable(this);
    }

    void setAsyncDatabaseAccessor_(AsyncDatabaseAccessor* async_db_accessor) { async_db_accessor_ = async_db_accessor; }

    std::string getDescription_() const override { return name_; }

    PipelineAction processOne(bool force) override final { return run_(force); }

    PipelineAction processAll(bool force) override final
    {
        PipelineAction outcome = PipelineAction::SLEEP;
        while (true)
        {
            auto result = processOne(force);
            if (result == PipelineAction::PROCEED)
            {
                outcome = PipelineAction::PROCEED;
            } else if (result == PipelineAction::SLEEP)
            {
                break;
            }
        }
        return outcome;
    }

    virtual PipelineAction run_(bool force) = 0;

    std::string name_;
    StageQueueRepo queue_repo_;
    const size_t interval_milliseconds_;
    AsyncDatabaseAccessor* async_db_accessor_ = nullptr;

    friend class Pipeline;
    friend class PipelineManager;
    friend class Flusher;
};

/*!
 * \class CompressionStage
 *
 * \brief For stages that perform zlib compression, subclassing from
 * CompressionStage adds an API to get the best compression level given
 * the thread's current pct time spent sleeping vs working.
 */
class CompressionStage : public Stage
{
public:
    // \brief Construct
    // \param force_compress_regardless If true, the minimum compression
    // level will always be 1 (fastest compression) even if the thread
    // is not keeping up (never sleeping). If false, level=0 can be
    // returned to denote "thread is too slow; do not compress"
    CompressionStage(bool force_compress_regardless = true) :
        force_compress_regardless_(force_compress_regardless)
    {
    }

protected:
    int getBestCompressionLevel_() const
    {
        const auto sleep_pct = std::clamp(getThread_()->getSleepPct(), 0.0, 100.0);

        // Idle threads can spend CPU on better compression; busy threads favor speed.
        const int min_level = force_compress_regardless_ ? static_cast<int>(CompressionLevel::FASTEST)
                                                         : static_cast<int>(CompressionLevel::DISABLED);
        const int max_level = static_cast<int>(CompressionLevel::HIGHEST);
        return min_level + static_cast<int>((max_level - min_level) * sleep_pct / 100.0 + 0.5);
    }

private:
    const bool force_compress_regardless_;
};

/*!
 * \class DatabaseStageBase
 *
 * \brief Base for stages that run on the dedicated DatabaseThread and use
 *        DatabaseAccessor (getDatabaseManager_(), getTableInserter_()). Do not
 *        use getAsyncDatabaseAccessor_() from a DatabaseStage.
 */
class DatabaseStageBase : public Stage
{
protected:
    AsyncDatabaseAccessor* getAsyncDatabaseAccessor_() const override final
    {
        throw DBException("Cannot access the AsyncDatabaseAccessor from a "
                          "DatabaseStage - use "
                          "getDatabaseManager_()");
    }
};

/*!
 * \class DatabaseStage
 *
 * \brief Concrete base for app-specific database stages. Provides
 *        getDatabaseManager_() and getTableInserter_() keyed by \p AppT.
 *        Derive from DatabaseStage<YourApp> and implement run_().
 * \tparam AppT The App type (for schema and table inserters).
 */
template <typename AppT> class DatabaseStage : public DatabaseStageBase
{
protected:
    DatabaseManager* getDatabaseManager_()
    {
        simdb_assert(db_accessor_, "DatabaseAccessor not initialized");
        return db_accessor_->getDatabaseManager();
    }

    PreparedINSERT* getTableInserter_(const std::string& tbl_name)
    {
        simdb_assert(db_accessor_, "DatabaseAccessor not initialized");
        return db_accessor_->template getTableInserter<AppT>(tbl_name);
    }

private:
    void assignThread_(DatabaseManager* db_mgr, std::vector<std::unique_ptr<PollingThread>>&,
                       std::unique_ptr<DatabaseThread>& database_thread) override final
    {
        // Prepare the DatabaseAccessor
        db_accessor_ = std::make_unique<DatabaseAccessor>(db_mgr);

        // Use dedicated database thread
        if (!database_thread)
        {
            database_thread = std::make_unique<DatabaseThread>(db_mgr);
        }

        database_thread->addRunnable(this);
    }

    std::unique_ptr<DatabaseAccessor> db_accessor_;
};

} // namespace simdb::pipeline
