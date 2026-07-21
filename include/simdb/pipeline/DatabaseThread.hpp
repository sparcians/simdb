// <DatabaseThread.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/AsyncDatabaseAccessor.hpp"
#include "simdb/pipeline/PollingThread.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

namespace simdb {
class AsyncDatabaseAccessor;
class DatabaseManager;
} // namespace simdb

namespace simdb::pipeline {

/*!
 * \class DatabaseThread
 *
 * \brief PollingThread dedicated to database stages. Runs all its Runnables
 *        inside batched BEGIN/COMMIT transactions for performance, and
 *        provides AsyncDatabaseAccessor for other threads to submit work
 *        (eval) that runs on this thread. Exactly one DatabaseThread exists
 *        for every pipeline associated with the same DatabaseManager (.db file).
 */
class DatabaseThread : public PollingThread, private AsyncDatabaseAccessHandler
{
public:
    /// \brief Construct the database thread for the given DatabaseManager.
    /// \param db_mgr DatabaseManager used for safeTransaction and async tasks.
    DatabaseThread(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr),
        dormant_thread_(db_mgr)
    {
    }

    ~DatabaseThread() noexcept = default;

    /// \brief Return the AsyncDatabaseAccessor for submitting work to this thread.
    AsyncDatabaseAccessor* getAsyncDatabaseAccessor() { return &db_accessor_; }

private:
    /// Overridden from AsyncDatabaseAccessHandler
    void eval(AsyncDatabaseTaskPtr&& task, double timeout_seconds = 0) override final
    {
        dormant_thread_.eval(std::move(task), timeout_seconds);
    }

    /// Overridden from PollingThread
    bool run_(bool force) override final
    {
        bool did_work = false;

        // Put all runnables (AsyncDbWriters) in a BEGIN/COMMIT block
        // for fastest insert performance.
        db_mgr_->safeTransaction([&]() {
            bool continue_while;
            do
            {
                continue_while = false;
                for (auto runnable : getRunnables())
                {
                    if (!runnable->enabled())
                    {
                        continue;
                    }

                    // We call processOne() here instead of processAll() to use
                    // a smaller granularity of tasks to "inject" break
                    // statements more frequently in the event of pending async
                    // DB access requests.
                    if (runnable->processOne(force) == PipelineAction::PROCEED)
                    {
                        continue_while = true;
                    }

                    // Give as high priority as possible for async DB access
                    if (dormant_thread_.hasTasks())
                    {
                        continue_while = false;
                        break;
                    }
                }
                did_work |= continue_while;
            } while (continue_while);
        });

        return did_work;
    }

    /// Overridden from PollingThread
    void open() override
    {
        PollingThread::open();
        dormant_thread_.open();
    }

    /// Overridden from PollingThread
    void close() noexcept override
    {
        PollingThread::close();
        dormant_thread_.close();
    }

    /// Overridden from PollingThread
    bool flushRunnables() override
    {
        bool did_work = false;

        db_mgr_->safeTransaction([&] { did_work = PollingThread::flushRunnables(); });

        return did_work;
    }

    /*!
     * \class DormantThread
     *
     * \brief Thread that sleeps until an async DB task is enqueued; then runs
     *        the task in a safeTransaction() and goes back to sleep. Used to
     *        service AsyncDatabaseAccessor::eval() without blocking the main
     *        DB polling loop.
     */
    class DormantThread
    {
    public:
        explicit DormantThread(DatabaseManager* db_mgr) :
            db_mgr_(db_mgr)
        {
        }

        ~DormantThread() noexcept { close(); }

        /// \brief Start the dormant thread (waits on condition variable for tasks).
        void open()
        {
            if (!thread_)
            {
                stop_ = false;
                thread_ = std::make_unique<std::thread>(&DormantThread::loop_, this);
            }
        }

        /// \brief Stop the thread and join.
        void close() noexcept
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }
            cond_var_.notify_one();

            if (thread_)
            {
                if (thread_->joinable())
                {
                    thread_->join();
                }
                thread_.reset();
            }
        }

        /// \brief Enqueue a task and optionally block until it completes (or timeout).
        /// \param task Task to run on this thread inside safeTransaction().
        /// \param timeout_seconds If > 0, block up to this many seconds; throw on timeout.
        /// \throws DBException on timeout or if the task throws.
        void eval(AsyncDatabaseTaskPtr&& task, double timeout_seconds = 0)
        {
            if (stop_)
            {
                // If we are already stopped, we cannot use std::future. We have
                // to evaluate the task right here.
                db_mgr_->safeTransaction([&]() { task->func(db_mgr_); });
                return;
            }

            std::future<std::string> fut = task->exception_reason.get_future();
            pending_async_db_tasks_.emplace(std::move(task));
            cond_var_.notify_one();

            if (timeout_seconds > 0)
            {
                auto status = fut.wait_for(std::chrono::duration<double>(timeout_seconds));
                simdb_assert(status != std::future_status::timeout, "Timed out waiting for async DB task to complete");
            }

            auto exception_reason = fut.get();
            simdb_assert(exception_reason.empty(), exception_reason);
        }

        /// \brief Return true if there are pending async DB tasks in the queue.
        bool hasTasks() const { return !pending_async_db_tasks_.empty(); }

    private:
        /// \brief Main loop that runs until the thread is stopped.
        void loop_()
        {
            while (true)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_var_.wait(lock, [this] { return !pending_async_db_tasks_.empty() || stop_; });

                // Assume we just woke up due to an async DB access request.
                // Immediately start a transaction so the DB polling thread
                // blocks until we are done responding to the requests.
                db_mgr_->safeTransaction([&]() {
                    AsyncDatabaseTaskPtr async_task;
                    while (pending_async_db_tasks_.try_pop(async_task))
                    {
                        try
                        {
                            async_task->func(db_mgr_);
                            async_task->exception_reason.set_value("");
                        } catch (const std::exception& ex)
                        {
                            async_task->exception_reason.set_value(ex.what());
                        }
                    }
                });

                if (stop_)
                {
                    break;
                }
            }
        }

        DatabaseManager* db_mgr_ = nullptr;
        std::unique_ptr<std::thread> thread_;
        std::mutex mutex_;
        std::condition_variable cond_var_;
        std::atomic<bool> stop_;
        ConcurrentQueue<AsyncDatabaseTaskPtr> pending_async_db_tasks_;
    };

    DatabaseManager* db_mgr_ = nullptr;
    AsyncDatabaseAccessor db_accessor_{this};
    std::vector<Runnable*> polling_runnables_;
    DormantThread dormant_thread_;
};

} // namespace simdb::pipeline
