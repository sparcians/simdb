// <DomainCollection.hpp> -*- C++ -*-

#pragma once

#include <memory>

#include "simdb/apps/argos_old/Timestamps.hpp"
#include "simdb/apps/argos_old/Collectables.hpp"
#include "simdb/apps/argos_old/CollectionBase.hpp"
#include "simdb/apps/argos_old/EnumDefinitions.hpp"

namespace simdb::collection {

/// \class DomainCollection
/// \brief Non-template base that \ref CollectableBase points at; \ref TimeDomainCollection is the
/// concrete per-clock implementation with timestamp + stager.
class DomainCollection
{
public:
    explicit DomainCollection(CollectionBase* collection_if)
        : collection_if_(collection_if)
    {}

    virtual ~DomainCollection() = default;

    /// \brief Let the \ref Collection object give us our collectables.
    /// Keep a separate structure to hold collectables that are to
    /// be auto-collected.
    void addCollectable(
        const std::string& path,
        std::shared_ptr<CollectableBase> collectable,
        bool auto_collect)
    {
        assert(collectable != nullptr);
        assert(std::find(all_collectables_.begin(), all_collectables_.end(), collectable) == all_collectables_.end());
        all_collectables_.push_back(collectable);
        collectables_by_path_[path] = collectable;

        if (auto_collect)
        {
            all_auto_collectables_.push_back(collectable.get());
        }
    }

    /// \brief Access the collectable at the given path
    const CollectableBase* getCollectable(const std::string& path, bool must_exist = true)
    {
        auto it = collectables_by_path_.find(path);
        if (it == collectables_by_path_.end())
        {
            if (must_exist)
            {
                throw DBException("Collectable does not exist at path: ") << path;
            }
            return nullptr;
        }
        return it->second.get();
    }

    /// \brief Get all collectables
    std::vector<const CollectableBase*> getAllCollectables() const
    {
        std::vector<const CollectableBase*> collectables;
        for (const auto& [_, collectable] : collectables_by_path_)
        {
            collectables.push_back(collectable.get());
        }
        return collectables;
    }

    /// \brief Get all collectable paths
    std::vector<std::string> getCollectablePaths() const
    {
        std::vector<std::string> paths;
        for (const auto& [path, _] : collectables_by_path_)
        {
            paths.push_back(path);
        }
        return paths;
    }

    bool minifiersSawAllActions() const
    {
        for (const auto& collectable : all_collectables_)
        {
            if (!collectable->minifierSawAllActions())
            {
                return false;
            }
        }
        return true;
    }

    /// \brief Connect the collectables to the CollectorPipeline's main input queue
    virtual void connectToPipeline(
        ConcurrentQueue<QueueCollectionData>* pipeline_head,
        EnumDefinitions* enum_definitions)
    {
        pipeline_head_ = pipeline_head;
        enum_definitions_ = enum_definitions;
    }

    /// \brief Run auto-collection on all collectables configured for it
    void performAutoCollection()
    {
        for (auto collectable : all_auto_collectables_)
        {
            if (collectable->enabled())
            {
                collectable->autoCollect();
            }
        }
    }

    /// \brief Calls to performAutoCollection(), just like all explicit
    /// calls to the collect() methods, only collect the data bytes and
    /// organize them by their timestamps. You must call this method
    /// to push the data down the pipeline.
    virtual void sendCollectedDataToPipeline() = 0;

    /// \brief Let subclasses provide timestamps
    virtual std::shared_ptr<TimePointBase> getCurrentTime() const { return nullptr; }

protected:
    DomainCollection() = default;

    const auto& getCollectables_() const
    {
        return all_collectables_;
    }

private:
    std::vector<std::shared_ptr<CollectableBase>> all_collectables_;
    std::vector<CollectableBase*> all_auto_collectables_;
    std::map<std::string, std::shared_ptr<CollectableBase>> collectables_by_path_;
    CollectionBase* collection_if_ = nullptr;
    ConcurrentQueue<QueueCollectionData>* pipeline_head_ = nullptr;
    EnumDefinitions* enum_definitions_ = nullptr;
};

/// \class TimeDomainCollection
/// \brief One clock domain: collectables plus shared \ref Timestamp and pipeline stager.
template <typename TimeT> class TimeDomainCollection : public DomainCollection
{
public:
    TimeDomainCollection(std::shared_ptr<Timestamp<TimeT>> timestamp, CollectionBase* collection_if)
        : DomainCollection(collection_if)
        , heartbeat_(collection_if->getHeartbeat())
        , timestamp_(std::move(timestamp))
    {}

    void connectToPipeline(
        ConcurrentQueue<QueueCollectionData>* pipeline_head,
        EnumDefinitions* enum_definitions) override final
    {
        stager_ = std::make_unique<PipelineStager<TimeT>>(heartbeat_, timestamp_.get(), pipeline_head);
        for (auto& collectable : getCollectables_())
        {
            collectable->connectToPipeline(stager_.get(), enum_definitions);
        }
        DomainCollection::connectToPipeline(pipeline_head, enum_definitions);
    }

    void sendCollectedDataToPipeline() override final
    {
        if (stager_)
        {
            stager_->sendCollectedDataToPipeline();
        }
    }

    std::shared_ptr<TimePointBase> getCurrentTime() const override final
    {
        return timestamp_->snapshot();
    }

private:
    const size_t heartbeat_;
    std::shared_ptr<Timestamp<TimeT>> timestamp_;
    std::unique_ptr<PipelineStager<TimeT>> stager_;
};

} // namespace simdb::collection
