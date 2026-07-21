// <QueueRepo.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/Queue.hpp"
#include <set>

namespace simdb::pipeline {

/*!
 * \class QueuePlaceholder
 *
 * \brief Abstract placeholder for a pipeline port queue. Used by StageQueueRepo
 *        and PipelineQueueRepo to create and bind ConcurrentQueues when stages
 *        are finalized and bindings are applied.
 */
class QueuePlaceholder
{
public:
    virtual ~QueuePlaceholder() = default;
    /// \brief Create a new Queue and return ownership.
    virtual std::unique_ptr<QueueBase> createQueue() = 0;
    /// \brief Assign an existing queue to this placeholder (e.g. after binding).
    virtual void assignQueue(QueueBase* queue) = 0;
    /// \brief Return true if a queue has been created or assigned.
    virtual bool hasQueue() const = 0;
};

/*!
 * \class InputQueuePlaceholder
 *
 * \brief Placeholder for an input port queue; holds a reference to the stage's
 *        ConcurrentQueue<T>* so the queue can be assigned when bound.
 * \tparam T Element type of the queue.
 */
template <typename T> class InputQueuePlaceholder : public QueuePlaceholder
{
public:
    /// \brief Construct with a reference to the stage's queue pointer (must be null initially).
    /// \param queue Reference to the stage's ConcurrentQueue<T>*; set when
    /// \throws DBException if \p queue is non-null.
    InputQueuePlaceholder(ConcurrentQueue<T>*& queue) :
        queue_(queue)
    {
        simdb_assert(!queue_, "Input queue placeholder cannot be initialized "
                              "with non-null queue pointer.");
    }

    std::unique_ptr<QueueBase> createQueue() override
    {
        auto queue = std::make_unique<Queue<T>>();
        queue_ = &queue->get();
        return queue;
    }

    void assignQueue(QueueBase* queue) override
    {
        auto typed_queue = dynamic_cast<Queue<T>*>(queue);
        simdb_assert(typed_queue, "Incompatible queue types in binding");
        queue_ = &typed_queue->get();
    }

    /// \brief Return true if a queue has been created or assigned.
    bool hasQueue() const override { return queue_ != nullptr; }

    /// \brief Return the assigned queue pointer (null until createQueue or assignQueue).
    ConcurrentQueue<T>* getQueue() { return queue_; }

private:
    ConcurrentQueue<T>*& queue_;
};

/*!
 * \class OutputQueuePlaceholder
 *
 * \brief Placeholder for an output port queue; holds a reference to the stage's
 *        ConcurrentQueue<T>* so the queue can be assigned when bound.
 * \tparam T Element type of the queue.
 */
template <typename T> class OutputQueuePlaceholder : public QueuePlaceholder
{
public:
    /// \brief Construct with a reference to the stage's queue pointer (must be null initially).
    /// \param queue Reference to the stage's ConcurrentQueue<T>*; set when noMoreStages() is called.
    /// \throws DBException if \p queue is non-null.
    OutputQueuePlaceholder(ConcurrentQueue<T>*& queue) :
        queue_(queue)
    {
        simdb_assert(!queue_, "Output queue placeholder cannot be initialized "
                              "with non-null queue pointer.");
    }

    /// \brief Create a new Queue and return ownership.
    std::unique_ptr<QueueBase> createQueue() override
    {
        auto queue = std::make_unique<Queue<T>>();
        queue_ = &queue->get();
        return queue;
    }

    /// \brief Assign an existing queue to this placeholder (e.g. after binding).
    void assignQueue(QueueBase* queue) override
    {
        auto typed_queue = dynamic_cast<Queue<T>*>(queue);
        simdb_assert(typed_queue, "Incompatible queue types in binding");
        queue_ = &typed_queue->get();
    }

    bool hasQueue() const override { return queue_ != nullptr; }

    /// \brief Return the assigned queue pointer (null until createQueue or assignQueue).
    ConcurrentQueue<T>* getQueue() { return queue_; }

private:
    ConcurrentQueue<T>*& queue_;
};

/*!
 * \class StageQueueRepo
 *
 * \brief Per-stage registry of input and output port placeholders. Used in
 *        Stage subclasses (addInPort_/addOutPort_) before the stage name is set.
 *        Keys become "stage_name.port_name" after setStageName().
 */
class StageQueueRepo
{
public:
    /// \brief Register an input port placeholder; only in Stage subclass constructors (before name is set).
    /// \throws DBException if stage name already set, or port name already exists.
    template <typename T> void addInPortPlaceholder(const std::string& port_name, ConcurrentQueue<T>*& queue)
    {
        simdb_assert(stage_name_.empty(), "You may only add in/out ports in Stage subclass constructors");

        auto& placeholder = input_placeholders_[port_name];
        simdb_assert(!placeholder, "Input port placeholder '" + port_name + "' already exists in QueueRepo");
        placeholder = std::make_unique<InputQueuePlaceholder<T>>(queue);
    }

    /// \brief Register an output port placeholder; only in Stage subclass constructors (before name is set).
    /// \throws DBException if stage name already set, or port name already exists.
    template <typename T> void addOutPortPlaceholder(const std::string& port_name, ConcurrentQueue<T>*& queue)
    {
        simdb_assert(stage_name_.empty(), "You may only add in/out ports in Stage subclass constructors");

        auto& placeholder = output_placeholders_[port_name];
        simdb_assert(!placeholder, "Output port placeholder '" + port_name + "' already exists in QueueRepo");
        placeholder = std::make_unique<OutputQueuePlaceholder<T>>(queue);
    }

    /// \brief Set the stage name; rekeys all placeholders to "stage_name.port_name". Call at most once.
    /// \throws DBException if renaming or if name already set differently.
    void setStageName(const std::string& stage_name)
    {
        simdb_assert(stage_name_ == stage_name || stage_name_.empty(),
                     "Cannot rename StageQueueRepo stage name from '" + stage_name_ + "' to '" + stage_name +
                         "'. Can only set the stage name once.");
        stage_name_ = stage_name;

        std::vector<std::string> keys_to_delete;
        for (auto& [key, placeholder] : input_placeholders_)
        {
            auto new_key = stage_name_ + "." + key;
            keys_to_delete.push_back(key);
            input_placeholders_[new_key] = std::move(placeholder);
        }

        for (const auto& key : keys_to_delete)
        {
            input_placeholders_.erase(key);
        }

        keys_to_delete.clear();
        for (auto& [key, placeholder] : output_placeholders_)
        {
            auto new_key = stage_name_ + "." + key;
            keys_to_delete.push_back(key);
            output_placeholders_[new_key] = std::move(placeholder);
        }

        for (const auto& key : keys_to_delete)
        {
            output_placeholders_.erase(key);
        }
    }

private:
    std::unordered_map<std::string, std::unique_ptr<QueuePlaceholder>> input_placeholders_;
    std::unordered_map<std::string, std::unique_ptr<QueuePlaceholder>> output_placeholders_;
    std::string stage_name_;
    friend class PipelineQueueRepo;
};

/*!
 * \class PipelineQueueRepo
 *
 * \brief Aggregates StageQueueRepos, manages port bindings, and creates/assigns
 *        queues in finalizeBindings(). Lifecycle: add stages via merge(), then
 *        noMoreStages(), then bind() pairs, then finalizeBindings(); after that
 *        getInPortQueue/getOutPortQueue and validateQueues() are valid.
 */
class PipelineQueueRepo
{
public:
    /// \brief Merge another stage's placeholders into this repo; only while accepting stages.
    /// \throws DBException if not accepting stages or if a port name collides.
    void merge(StageQueueRepo& other)
    {
        simdb_assert(state_ == RepoState::ACCEPTING_STAGES, "Cannot merge StageQueueRepo; PipelineQueueRepo "
                                                            "not accepting stages.");

        for (auto& [key, placeholder] : other.input_placeholders_)
        {
            simdb_assert(input_placeholders_.find(key) == input_placeholders_.end(),
                         "Input port placeholder '" + key + "' already exists in QueueRepo");
            input_placeholders_[key] = std::move(placeholder);
        }

        for (auto& [key, placeholder] : other.output_placeholders_)
        {
            simdb_assert(output_placeholders_.find(key) == output_placeholders_.end(),
                         "Output port placeholder '" + key + "' already exists in QueueRepo");
            output_placeholders_[key] = std::move(placeholder);
        }

        other.input_placeholders_.clear();
        other.output_placeholders_.clear();
    }

    /// \brief Signal that no more stages will be merged; switch to accepting bindings.
    /// \throws DBException if not accepting stages.
    void noMoreStages()
    {
        simdb_assert(state_ == RepoState::ACCEPTING_STAGES, "Cannot finalize stages; PipelineQueueRepo not "
                                                            "accepting stages.");
        state_ = RepoState::ACCEPTING_BINDINGS;
    }

    /// \brief Bind an output port to an input port (both full names, e.g. "StageA.out" -> "StageB.in").
    /// \throws DBException if not accepting bindings.
    void bind(const std::string& output_port_full_name, const std::string& input_port_full_name)
    {
        simdb_assert(state_ == RepoState::ACCEPTING_BINDINGS,
                     "Cannot bind ports; PipelineQueueRepo not accepting bindings.");
        port_bindings_[output_port_full_name] = input_port_full_name;
    }

    /// \brief Create queues from bindings and unbound ports; switch to bindings complete.
    /// \throws DBException if not accepting bindings or if a port is missing.
    void finalizeBindings()
    {
        simdb_assert(state_ == RepoState::ACCEPTING_BINDINGS, "Cannot finalize bindings; PipelineQueueRepo not "
                                                              "accepting bindings.");

        for (const auto& [out_port, in_port] : port_bindings_)
        {
            auto out_it = output_placeholders_.find(out_port);
            simdb_assert(out_it != output_placeholders_.end(),
                         "Output port placeholder '" + out_port + "' not found in QueueRepo");
            auto in_it = input_placeholders_.find(in_port);
            simdb_assert(in_it != input_placeholders_.end(),
                         "Input port placeholder '" + in_port + "' not found in QueueRepo");

            auto queue = out_it->second->createQueue();
            queues_.emplace_back(std::move(queue));
            in_it->second->assignQueue(queues_.back().get());
        }

        for (const auto& [key, placeholder] : input_placeholders_)
        {
            if (!placeholder->hasQueue())
            {
                auto queue = placeholder->createQueue();
                queues_.emplace_back(std::move(queue));
                unbound_input_queues_.insert(key);
            }
        }

        for (const auto& [key, placeholder] : output_placeholders_)
        {
            if (!placeholder->hasQueue())
            {
                auto queue = placeholder->createQueue();
                queues_.emplace_back(std::move(queue));
                unbound_output_queues_.insert(key);
            }
        }

        state_ = RepoState::BINDINGS_COMPLETE;
    }

    /// \brief Return the input port queue for the given full port name; only after finalizeBindings().
    /// \tparam T Element type of the queue.
    /// \throws DBException if bindings not finalized, port not found, or type mismatch.
    template <typename T> ConcurrentQueue<T>* getInPortQueue(const std::string& port_full_name)
    {
        simdb_assert(state_ == RepoState::BINDINGS_COMPLETE, "Cannot access port queues until "
                                                             "finalizeBindings() is called.");

        auto it = input_placeholders_.find(port_full_name);
        simdb_assert(it != input_placeholders_.end(),
                     "Input port placeholder '" + port_full_name + "' not found in PipelineQueueRepo");
        auto typed_placeholder = dynamic_cast<InputQueuePlaceholder<T>*>(it->second.get());
        simdb_assert(typed_placeholder, "Incompatible queue types for input port '" + port_full_name + "'");
        unbound_input_queues_.erase(port_full_name);
        return typed_placeholder->getQueue();
    }

    /// \brief Return the output port queue for the given full port name; only after finalizeBindings().
    /// \tparam T Element type of the queue.
    /// \throws DBException if bindings not finalized, port not found, or type mismatch.
    template <typename T> ConcurrentQueue<T>* getOutPortQueue(const std::string& port_full_name)
    {
        simdb_assert(state_ == RepoState::BINDINGS_COMPLETE, "Cannot access port queues until "
                                                             "finalizeBindings() is called.");

        auto it = output_placeholders_.find(port_full_name);
        simdb_assert(it != output_placeholders_.end(),
                     "Output port placeholder '" + port_full_name + "' not found in PipelineQueueRepo");
        auto typed_placeholder = dynamic_cast<OutputQueuePlaceholder<T>*>(it->second.get());
        simdb_assert(typed_placeholder, "Incompatible queue types for output port '" + port_full_name + "'");
        unbound_output_queues_.erase(port_full_name);
        return typed_placeholder->getQueue();
    }

    /// \brief Throw if any input or output port is unbound (not connected and not explicitly unbound).
    /// \throws DBException with a message listing unbound ports.
    void validateQueues()
    {
        std::ostringstream oss;

        if (!unbound_input_queues_.empty())
        {
            oss << "The following input queues are not attached to anything:\n";
            for (const auto& name : unbound_input_queues_)
            {
                oss << "    " << name << "\n";
            }
        }

        if (!unbound_output_queues_.empty())
        {
            oss << "The following output queues are not attached to "
                   "anything:\n";
            for (const auto& name : unbound_output_queues_)
            {
                oss << "    " << name << "\n";
            }
        }

        auto err = oss.str();
        simdb_assert(err.empty(), err);
    }

private:
    std::unordered_map<std::string, std::unique_ptr<QueuePlaceholder>> input_placeholders_;
    std::unordered_map<std::string, std::unique_ptr<QueuePlaceholder>> output_placeholders_;
    std::unordered_map<std::string, std::string> port_bindings_;
    std::set<std::string> unbound_input_queues_;
    std::set<std::string> unbound_output_queues_;
    std::vector<std::unique_ptr<QueueBase>> queues_;

    enum class RepoState { ACCEPTING_STAGES, ACCEPTING_BINDINGS, BINDINGS_COMPLETE };

    RepoState state_ = RepoState::ACCEPTING_STAGES;
};

} // namespace simdb::pipeline
