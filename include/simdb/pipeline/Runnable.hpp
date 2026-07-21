// <Runnable.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace simdb::pipeline {

class PipelineManager;
class PollingThread;

/// \brief Outcome of processOne() / processAll(): whether the runnable did work or the thread may sleep.
enum PipelineAction {
    /// Runnable did work (or should keep the thread busy); thread continues without sleeping.
    PROCEED,
    /// Runnable had nothing to do; if all runnables on the thread return SLEEP, the thread sleeps.
    SLEEP
};

/*!
 * \class Runnable
 *
 * \brief Base class for units of work on a pipeline thread (e.g. Stage). PollingThread
 *        calls processOne() / processAll(); PROCEED means work was done, SLEEP means
 *        none. Supports enable/disable and a human-readable description for reporting.
 */
class Runnable
{
public:
    virtual ~Runnable() = default;

    /// \brief Return the runnable's description (for logging/reports); uses set value or getDescription_().
    std::string getDescription() const { return !description_.empty() ? description_ : getDescription_(); }

    /// \brief Set or overwrite the runnable's description.
    /// \param desc Description string.
    void setDescription(const std::string& desc) { description_ = desc; }

    /// \brief Process one unit of work; return PROCEED if work was done, SLEEP otherwise.
    /// \param force If true, run even when there is no input (e.g. for flushing).
    virtual PipelineAction processOne(bool force) = 0;

    /// \brief Process all pending work; return PROCEED if any work was done, SLEEP otherwise.
    /// \param force If true, run until no more work (e.g. for flushing).
    virtual PipelineAction processAll(bool force) = 0;

    /// \brief Print a one-line description to \p os with \p indent spaces (for perf reports).
    virtual void print(std::ostream& os, int indent) const
    {
        os << std::string(indent, ' ') << getDescription() << "\n";
    }

    /// \brief Return true if this runnable is enabled (disabled runnables are skipped by the thread).
    bool enabled() const { return enabled_; }

    /// \brief Enable or disable this runnable.
    /// \param enable true to enable, false to disable.
    void enable(bool enable = true) { enabled_ = enable; }

protected:
    const PollingThread* getThread_() const
    {
        simdb_assert(thread_, "PollingThread never set");
        return thread_;
    }

private:
    virtual std::string getDescription_() const = 0;
    std::string description_;
    bool enabled_ = true;

    const PollingThread* thread_ = nullptr;
    friend class PollingThread;
};

/*!
 * \class ScopedRunnableDisabler
 *
 * \brief RAII guard that disables the given runnables (and optionally pauses
 *        the given polling threads) on construction and re-enables/resumes
 *        them on destruction. Obtained from PipelineManager::scopedDisableAll().
 */
class ScopedRunnableDisabler
{
public:
    ~ScopedRunnableDisabler();

private:
    ScopedRunnableDisabler(PipelineManager* pipeline_mgr, const std::vector<Runnable*>& runnables,
                           const std::vector<PollingThread*>& polling_threads);

    ScopedRunnableDisabler(PipelineManager* pipeline_mgr, const std::vector<Runnable*>& runnables);

    void notifyPipelineMgrReenabled_();

    PipelineManager* pipeline_mgr_ = nullptr;
    std::vector<Runnable*> disabled_runnables_;
    std::vector<PollingThread*> paused_threads_;
    friend class PipelineManager;
};

} // namespace simdb::pipeline
