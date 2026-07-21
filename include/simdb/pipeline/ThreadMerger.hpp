// <ThreadMerger.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/DatabaseThread.hpp"
#include "simdb/pipeline/Pipeline.hpp"
#include "simdb/pipeline/PollingThread.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace simdb::pipeline {

/*!
 * \class ThreadMerger
 *
 * \brief Merges non-database PollingThreads across pipelines when
 *        PipelineManager::minimizeThreads() or minimizeThreads(app,...) was
 *        called before openPipelines(). Creates a minimal set of threads.
 *        Exactly one DatabaseThread is shared by all pipeline(s) DatabaseStages
 *        if any.
 */
class ThreadMerger
{
public:
    /// \brief Construct with the pipelines that will contribute stages.
    /// \param pipelines All pipelines (from PipelineManager).
    ThreadMerger(const std::vector<std::unique_ptr<Pipeline>>& pipelines) :
        pipelines_(pipelines)
    {
    }

    /// \brief Mark one app's non-database stages for merging; cannot mix with mergeAllAppThreads().
    /// \param app App whose pipeline threads should be merged.
    /// \throws DBException if already finalized or if mergeAllAppThreads() was used.
    void addAppForMerging(const App* app)
    {
        simdb_assert(accepting_apps_, "ThreadMerger already finalized merge");

        simdb_assert(!merge_all_apps_, "Can either call addAppForMerging() or "
                                       "mergeAllAppThreads(), not both");

        auto it = std::find(apps_to_merge_.begin(), apps_to_merge_.end(), app);
        simdb_assert(it == apps_to_merge_.end(), "Already tracking app for thread merging");
        apps_to_merge_.push_back(app);
    }

    /// \brief Merge all apps' non-database threads into a minimal set; cannot mix with addAppForMerging().
    void mergeAllAppThreads() { merge_all_apps_ = true; }

    /// \brief Build the final list of PollingThreads (and one DatabaseThread); call once from
    /// PipelineManager::openPipelines().
    /// \param polling_threads Output vector; merged threads are appended (caller takes ownership).
    void performMerge(std::vector<std::unique_ptr<PollingThread>>& polling_threads)
    {
        if (apps_to_merge_.empty() && !merge_all_apps_)
        {
            createThreadsWithoutMerging_(polling_threads);
        } else if (apps_to_merge_.empty() || apps_to_merge_.size() == pipelines_.size())
        {
            // TODO cnyce: I think this logic breaks down for apps that have
            // more than one pipeline.
            createThreadsAndMergeAll_(polling_threads);
        } else
        {
            createThreadsAndMergeSpecificApps_(polling_threads);
        }

        // Sanity check that we did not create more than one database thread.
        size_t num_db_threads = 0;
        for (const auto& thread : polling_threads)
        {
            if (dynamic_cast<const DatabaseThread*>(thread.get()))
            {
                ++num_db_threads;
            }
        }

        simdb_assert(num_db_threads <= 1, "Internal error - we ended up creating "
                                              << num_db_threads << " database threads! Only one is allowed.");

        // Sanity check that every runnable (stage) exists only on one thread.
        std::map<Runnable*, std::vector<PollingThread*>> threads_by_runnable;
        for (auto& thread : polling_threads)
        {
            for (auto runnable : thread->getRunnables())
            {
                threads_by_runnable[runnable].push_back(thread.get());
            }
        }

        for (const auto& [runnable, threads] : threads_by_runnable)
        {
            simdb_assert(threads.size() == 1, "Internal error - assigned pipeline stage to "
                                              "more than one thread");
        }

        // Reorder stages in polling threads to ensure that the relative
        // ordering of stages is the same in the threads as they were originally
        // defined in the app createPipeline() method.
        std::vector<Runnable*> ordered_runnables;
        for (auto& pipeline : pipelines_)
        {
            for (auto& [stage_name, stage] : pipeline->getOrderedStages())
            {
                ordered_runnables.push_back(stage);
            }
        }

        for (auto& thread : polling_threads)
        {
            thread->ensureRelativeOrder(ordered_runnables);
        }

        accepting_apps_ = false;
    }

private:
    void createThreadsWithoutMerging_(std::vector<std::unique_ptr<PollingThread>>& polling_threads)
    {
        std::unique_ptr<DatabaseThread> database_thread;
        for (auto& pipeline : pipelines_)
        {
            pipeline->assignStageThreads(polling_threads, database_thread);
        }

        // Add the dedicated database thread
        if (database_thread)
        {
            polling_threads.emplace_back(std::move(database_thread));
        }
    }

    void createThreadsAndMergeAll_(std::vector<std::unique_ptr<PollingThread>>& polling_threads)
    {
        // Create all threads for each app before merge.
        std::map<const App*, std::vector<std::unique_ptr<PollingThread>>> polling_threads_by_app;
        std::map<const App*, Pipeline*> pipelines_by_app;
        std::unique_ptr<DatabaseThread> database_thread;
        for (auto& pipeline : pipelines_)
        {
            auto& app_polling_threads = polling_threads_by_app[pipeline->getOwningApp()];
            pipeline->assignStageThreads(app_polling_threads, database_thread);
            pipelines_by_app[pipeline->getOwningApp()] = pipeline.get();
        }

        // Verify that all non-database stages are using the same polling
        // interval.
        std::set<size_t> intervals;
        for (const auto& [app, app_polling_threads] : polling_threads_by_app)
        {
            for (const auto& thread : app_polling_threads)
            {
                intervals.insert(thread->getIntervalMilliseconds());
            }
        }

        simdb_assert(intervals.size() == 1, "In order to merge threads, all must agree on "
                                            "their polling interval.");

        // Find the maximum number of polling threads needed for all apps.
        size_t max_polling_threads = 0;
        for (const auto& [app, app_polling_threads] : polling_threads_by_app)
        {
            max_polling_threads = std::max(max_polling_threads, app_polling_threads.size());
        }

        // Create the max number of polling threads needed for all apps.
        while (max_polling_threads--)
        {
            polling_threads.emplace_back(std::make_unique<PollingThread>());
        }

        // Reassign all the polling_threads_by_app stages (runnables) to the
        // merged thread list in a round-robin fashion.
        size_t thread_idx = 0;
        for (const auto& [app, app_polling_threads] : polling_threads_by_app)
        {
            auto app_pipeline = pipelines_by_app.at(app);
            for (const auto& [name, stage] : app_pipeline->getOrderedStages())
            {
                // Remember we are only merging non-database stages
                if (dynamic_cast<DatabaseStageBase*>(stage))
                {
                    continue;
                }

                auto& dest_thread = polling_threads.at(thread_idx);
                dest_thread->addRunnable(stage);

                ++thread_idx;
                if (thread_idx == polling_threads.size())
                {
                    thread_idx = 0;
                }
            }
        }

        // Add the dedicated database thread
        if (database_thread)
        {
            polling_threads.emplace_back(std::move(database_thread));
        }
    }

    void createThreadsAndMergeSpecificApps_(std::vector<std::unique_ptr<PollingThread>>& polling_threads)
    {
        // TODO cnyce: This logic is not tested well enough to enable this
        // feature yet.
        throw DBException("Method not yet supported");

        // Create all threads for each app before merge.
        std::map<const App*, std::vector<std::unique_ptr<PollingThread>>> polling_threads_by_app;
        std::map<const App*, Pipeline*> pipelines_by_app;
        std::unique_ptr<DatabaseThread> database_thread;
        for (auto& pipeline : pipelines_)
        {
            auto& app_polling_threads = polling_threads_by_app[pipeline->getOwningApp()];
            pipeline->assignStageThreads(app_polling_threads, database_thread);
            pipelines_by_app[pipeline->getOwningApp()] = pipeline.get();
        }

        // Find the maximum number of polling threads needed for all merged
        // apps.
        size_t max_polling_threads_merged = 0;
        for (const auto& [app, app_polling_threads] : polling_threads_by_app)
        {
            auto it = std::find(apps_to_merge_.begin(), apps_to_merge_.end(), app);
            auto merge = (it != apps_to_merge_.end());
            if (merge)
            {
                max_polling_threads_merged = std::max(max_polling_threads_merged, app_polling_threads.size());
            }
        }

        // Add the number of polling threads needed for all unmerged apps.
        size_t total_num_polling_threads = max_polling_threads_merged;
        for (const auto& [app, app_polling_threads] : polling_threads_by_app)
        {
            auto it = std::find(apps_to_merge_.begin(), apps_to_merge_.end(), app);
            auto merge = (it != apps_to_merge_.end());
            if (!merge)
            {
                total_num_polling_threads += app_polling_threads.size();
            }
        }

        // Create all polling threads needed.
        while (total_num_polling_threads--)
        {
            polling_threads.emplace_back(std::make_unique<PollingThread>());
        }

        // Assign stages from unmerged apps into the polling threads.
        size_t thread_idx = 0;
        for (const auto& [app, app_polling_threads] : polling_threads_by_app)
        {
            auto it = std::find(apps_to_merge_.begin(), apps_to_merge_.end(), app);
            auto merge = (it != apps_to_merge_.end());
            if (!merge)
            {
                auto app_pipeline = pipelines_by_app.at(app);
                for (const auto& [name, stage] : app_pipeline->getOrderedStages())
                {
                    // We are only merging threads for non-DB stages
                    if (dynamic_cast<DatabaseStageBase*>(stage))
                    {
                        continue;
                    }

                    auto& dest_thread = polling_threads.at(thread_idx);
                    dest_thread->addRunnable(stage);

                    ++thread_idx;
                    if (thread_idx == polling_threads.size())
                    {
                        thread_idx = 0;
                    }
                }
            }
        }

        // Perform merge
        for (auto app : apps_to_merge_)
        {
            auto app_pipeline = pipelines_by_app.at(app);
            for (const auto& [name, stage] : app_pipeline->getOrderedStages())
            {
                // Instead of a blind round-robin, sort the polling threads such
                // that the one with the fewest assigned stages gets the next
                // stage.
                std::sort(
                    polling_threads.begin(), polling_threads.end(),
                    [](const std::unique_ptr<PollingThread>& thread1, const std::unique_ptr<PollingThread>& thread2) {
                        return thread1->getNumRunnables() < thread2->getNumRunnables();
                    });

                auto& dest_thread = polling_threads.front();
                dest_thread->addRunnable(stage);
            }
        }

        // Add the dedicated database thread
        if (database_thread)
        {
            polling_threads.emplace_back(std::move(database_thread));
        }
    }

    const std::vector<std::unique_ptr<Pipeline>>& pipelines_;
    std::vector<const App*> apps_to_merge_;
    bool accepting_apps_ = true;
    bool merge_all_apps_ = false;
};

} // namespace simdb::pipeline
