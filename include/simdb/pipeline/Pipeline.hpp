// <Pipeline.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/Flusher.hpp"
#include "simdb/pipeline/Stage.hpp"
#include <map>

namespace simdb {
class App;
class DatabaseManager;
} // namespace simdb

namespace simdb::pipeline {

/*!
 * \class Pipeline
 *
 * \brief Multi-stage data processor with typed input/output ports between
 *        stages. Add stages, declare port bindings, then finalize; stages
 *        run on PollingThreads. Move-only semantics; no restriction on
 *        I/O type changes between stages.
 *
 * \note Create via PipelineManager::createPipeline().
 */
class Pipeline
{
public:
    /// \brief Construct a pipeline (typically via PipelineManager::createPipeline()).
    /// \param db_mgr DatabaseManager for database stages.
    /// \param name Pipeline name.
    /// \param app The App that owns this pipeline.
    Pipeline(DatabaseManager* db_mgr, const std::string& name, const App* app) :
        db_mgr_(db_mgr),
        pipeline_name_(name),
        app_(app)
    {
    }

    /// \brief Return the App that owns this pipeline.
    const App* getOwningApp() const { return app_; }

    /// \brief Return the DatabaseManager used by database stages.
    DatabaseManager* getDatabaseManager() const { return db_mgr_; }

    /// \brief Return the pipeline name.
    std::string getName() const { return pipeline_name_; }

    /// \brief Add a stage; only valid before noMoreStages(). Returns pointer to the new stage.
    /// \tparam StageType Stage class (must derive from Stage).
    /// \tparam StageCtorArgs Constructor argument types.
    /// \param name Unique stage name within this pipeline.
    /// \param args Arguments forwarded to the stage constructor.
    /// \throws DBException if not accepting stages or stage name already exists.
    template <typename StageType, typename... StageCtorArgs>
    StageType* addStage(const std::string& name, StageCtorArgs&&... args)
    {
        simdb_assert(state_ == State::ACCEPTING_STAGES,
                     "Cannot add stage '" + name + "' to pipeline '" + pipeline_name_ + "'; not accepting stages.");

        auto& stage = stages_[name];
        simdb_assert(!stage, "Stage '" + name + "' already exists in pipeline '" + pipeline_name_ + "'.");
        stage = std::make_unique<StageType>(std::forward<StageCtorArgs>(args)...);
        stage->setName_(name);
        stages_in_order_.push_back(name);
        return static_cast<StageType*>(stage.get());
    }

    /// \brief Finalize stages and start accepting bindings.
    /// \throws DBException if not accepting stages.
    void noMoreStages()
    {
        simdb_assert(state_ == State::ACCEPTING_STAGES,
                     "Cannot finalize stages for pipeline '" + pipeline_name_ + "'; stage changes already finalized.");
        for (auto& [stage_name, stage] : stages_)
        {
            stage->mergeQueueRepo_(queue_repo_);
        }
        queue_repo_.noMoreStages();
        state_ = State::ACCEPTING_BINDINGS;
    }

    /// \brief Bind an output port to an input port (full names: "StageName.port").
    /// \throws DBException if not accepting bindings.
    void bind(const std::string& output_port_full_name, const std::string& input_port_full_name)
    {
        simdb_assert(state_ == State::ACCEPTING_BINDINGS,
                     "Cannot bind ports for pipeline '" + pipeline_name_ + "'; not accepting bindings.");
        queue_repo_.bind(output_port_full_name, input_port_full_name);
    }

    /// \brief Finalize bindings and create queues; required before getInPortQueue/getOutPortQueue.
    /// \throws DBException if not accepting bindings.
    void noMoreBindings()
    {
        simdb_assert(state_ == State::ACCEPTING_BINDINGS, "Cannot finalize bindings for pipeline '" + pipeline_name_ +
                                                              "'; binding changes already finalized.");
        queue_repo_.finalizeBindings();
        state_ = State::BINDINGS_COMPLETE;
    }

    /// \brief Return the input port queue; only valid after noMoreBindings().
    /// \tparam T Element type of the queue.
    template <typename T> simdb::ConcurrentQueue<T>* getInPortQueue(const std::string& port_full_name)
    {
        simdb_assert(state_ == State::BINDINGS_COMPLETE || state_ == State::FINALIZED,
                     "Cannot access port queues until noMoreBindings() is called.");
        return queue_repo_.getInPortQueue<T>(port_full_name);
    }

    /// \brief Return the output port queue; only valid after noMoreBindings().
    /// \tparam T Element type of the queue.
    template <typename T> simdb::ConcurrentQueue<T>* getOutPortQueue(const std::string& port_full_name)
    {
        simdb_assert(state_ == State::BINDINGS_COMPLETE || state_ == State::FINALIZED,
                     "Cannot access port queues until noMoreBindings() is called.");
        return queue_repo_.getOutPortQueue<T>(port_full_name);
    }

    /// \brief Assign each stage to a PollingThread (or the shared DatabaseThread); call after noMoreBindings().
    /// \param threads Vector to which new PollingThreads may be appended.
    /// \param database_thread Single shared DatabaseThread for all DatabaseStages (created if null).
    void assignStageThreads(std::vector<std::unique_ptr<PollingThread>>& threads,
                            std::unique_ptr<DatabaseThread>& database_thread)
    {
        simdb_assert(state_ == State::BINDINGS_COMPLETE,
                     "Cannot assign stage threads for pipeline '" + pipeline_name_ + "; noMoreBindings() never called");

        queue_repo_.validateQueues();

        for (auto& [stage_name, stage] : stages_)
        {
            stage->assignThread_(db_mgr_, threads, database_thread);
        }

        state_ = State::FINALIZED;
    }

    /// \brief Create a Flusher that runs the given stages (or all in order); wraps in transaction
    /// if any stage is a DatabaseStage.
    /// \param stage_names Stage names in flush order; if empty, use add order.
    /// \return Flusher or FlusherWithTransaction; caller owns.
    /// \throws DBException if a stage name does not exist.
    std::unique_ptr<Flusher> createFlusher(const std::vector<std::string>& stage_names = {})
    {
        std::vector<Stage*> stages;
        const auto& ordered_stage_names = !stage_names.empty() ? stage_names : stages_in_order_;
        for (const auto& name : ordered_stage_names)
        {
            auto it = stages_.find(name);
            simdb_assert(it != stages_.end(), "Stage does not exist: " << name);
            stages.emplace_back(it->second.get());
        }

        bool has_db_stage = false;
        for (auto stage : stages)
        {
            if (dynamic_cast<DatabaseStageBase*>(stage))
            {
                has_db_stage = true;
            }
        }

        return has_db_stage ? std::unique_ptr<FlusherWithTransaction>(new FlusherWithTransaction(stages, db_mgr_))
                            : std::unique_ptr<Flusher>(new Flusher(stages));
    }

    /// \brief Return the AsyncDatabaseAccessor for this pipeline (set by PipelineManager after openPipelines()).
    AsyncDatabaseAccessor* getAsyncDatabaseAccessor() const { return async_db_accessor_; }

    /// \brief Return a map of stage name to Stage* (all stages in this pipeline).
    std::map<std::string, Stage*> getStages()
    {
        std::map<std::string, Stage*> stages;
        for (auto& [name, stage] : stages_)
        {
            stages[name] = stage.get();
        }
        return stages;
    }

    /// \brief Return stages in the order they were added (name, Stage* pairs).
    std::vector<std::pair<std::string, Stage*>> getOrderedStages()
    {
        std::vector<std::pair<std::string, Stage*>> ordered_stages;
        auto unordered_stages = getStages();
        for (auto& name : stages_in_order_)
        {
            auto stage = unordered_stages.at(name);
            ordered_stages.emplace_back(std::make_pair(name, stage));
        }
        return ordered_stages;
    }

private:
    DatabaseManager* db_mgr_ = nullptr;
    std::string pipeline_name_;
    const App* app_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Stage>> stages_;
    std::vector<std::string> stages_in_order_;
    PipelineQueueRepo queue_repo_;
    AsyncDatabaseAccessor* async_db_accessor_ = nullptr;

    enum class State { ACCEPTING_STAGES, ACCEPTING_BINDINGS, BINDINGS_COMPLETE, FINALIZED };

    State state_ = State::ACCEPTING_STAGES;
};

} // namespace simdb::pipeline
