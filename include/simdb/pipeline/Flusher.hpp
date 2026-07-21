// <Flusher.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/Stage.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

namespace simdb::pipeline {

/*!
 * \class Flusher
 *
 * \brief Utility to run pipeline stages in order until no more work (processAll
 *        with force). Obtain from Pipeline::createFlusher(). Use when you need
 *        to drain queues or sync stages outside the normal polling loop.
 */
class Flusher
{
protected:
    /// \brief Construct with the ordered list of stages to flush (internal; use Pipeline::createFlusher()).
    Flusher(const std::vector<Stage*>& stages) :
        stages_(stages)
    {
        simdb_assert(!stages_.empty(), "No stages given to Flusher");
    }

    friend class Pipeline;

public:
    virtual ~Flusher() = default;

    /// \brief Run all stages with processAll(true) in order until none return PROCEED.
    /// \return PROCEED if any stage did work, SLEEP otherwise.
    virtual PipelineAction flush()
    {
        PipelineAction outcome = PipelineAction::SLEEP;

        bool continue_while = true;
        do
        {
            continue_while = false;
            for (auto stage : stages_)
            {
                constexpr auto force = true;
                if (stage->processAll(force) == PipelineAction::PROCEED)
                {
                    outcome = PipelineAction::PROCEED;
                    continue_while = true;
                }
            }
        } while (continue_while);

        return outcome;
    }

private:
    std::vector<Stage*> stages_;
};

/*!
 * \class FlusherWithTransaction
 *
 * \brief Flusher that runs flush() inside a single safeTransaction() when any
 *        of the stages are DatabaseStages. Used by Pipeline::createFlusher()
 *        automatically when needed; reduces many small transactions to one.
 */
class FlusherWithTransaction : public Flusher
{
private:
    /// \brief Internal constructor (Pipeline::createFlusher() uses this when a database stage is present).
    FlusherWithTransaction(const std::vector<Stage*>& stages, DatabaseManager* db_mgr) :
        Flusher(stages),
        db_mgr_(db_mgr)
    {
        bool has_db_stage = false;
        for (auto stage : stages)
        {
            if (dynamic_cast<DatabaseStageBase*>(stage))
            {
                has_db_stage = true;
            }
        }

        simdb_assert(has_db_stage, "Cannot use FlusherWithTransaction - there are "
                                   "no database stages!");
    }

    friend class Pipeline;

public:
    /// \brief Run flush() inside a single BEGIN/COMMIT transaction.
    /// \return PROCEED if any stage did work, SLEEP otherwise.
    PipelineAction flush() override
    {
        auto outcome = PipelineAction::SLEEP;
        db_mgr_->safeTransaction([&]() { outcome = Flusher::flush(); });
        return outcome;
    }

private:
    DatabaseManager* db_mgr_ = nullptr;
};

} // namespace simdb::pipeline
