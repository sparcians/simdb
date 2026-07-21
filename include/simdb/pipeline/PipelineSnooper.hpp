// <PipelineSnooper.hpp> -*- C++ -*-

#pragma once

#include <functional>
#include <set>
#include <vector>

#include "simdb/Assert.hpp"

namespace simdb::pipeline {

class PipelineManager;
class Stage;

/*!
 * \class PipelineSnooper
 *
 * \brief Collects Stage::snoop(key, snooped_obj) callbacks and runs them
 *        via snoopAllStages(). Create with PipelineManager::createSnooper<KeyType,SnoopedType>(),
 *        addStage() for each stage that implements snoop(), then call
 *        snoopAllStages() to query all stages (optionally with pipeline disabled).
 * \tparam KeyType Type of the key passed to snoop().
 * \tparam SnoopedType Type of the object that stages fill in (e.g. aggregate result).
 */
template <typename KeyType, typename SnoopedType> class PipelineSnooper
{
public:
    /// \brief Construct with the PipelineManager (used for snoopAllStages to optionally disable pipeline).
    /// \param pipeline_mgr Non-null PipelineManager.
    explicit PipelineSnooper(PipelineManager* pipeline_mgr) :
        pipeline_mgr_(pipeline_mgr)
    {
    }

    /// \brief Register a stage's snoop method; StageType must have void snoop(const KeyType&, SnoopedType&).
    /// \tparam StageType Stage-derived type that implements snoop().
    /// \param stage Stage to call snoop() on.
    /// \throws DBException if this stage was already added.
    template <typename StageType> void addStage(StageType* stage)
    {
        static_assert(std::is_base_of<Stage, StageType>::value);
        simdb_assert(snooped_stages_.insert(stage).second, "Already snooping stage!");

        auto cb = std::bind(&StageType::snoop, stage, std::placeholders::_1, std::placeholders::_2);
        callbacks_.push_back(cb);
    }

    /// \brief Call each stage's snoop(key, snooped_obj) until one returns true or all are tried.
    /// \param key Key to pass to snoop (e.g. lookup key).
    /// \param snooped_obj Object that stages may fill; passed by reference to each snoop().
    /// \param disable_pipeline If true, use scopedDisableAll() while snooping so pipeline is paused.
    /// \return true if any stage's snoop() returned true, false otherwise.
    bool snoopAllStages(const KeyType& key, SnoopedType& snooped_obj, bool disable_pipeline = true);

private:
    using Callback = std::function<bool(const KeyType&, SnoopedType&)>;
    std::vector<Callback> callbacks_;
    std::set<Stage*> snooped_stages_;
    PipelineManager* pipeline_mgr_ = nullptr;
};

} // namespace simdb::pipeline
