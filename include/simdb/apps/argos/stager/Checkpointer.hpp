// <Checkpointer.hpp> -*- C++ -*-

#pragma once

#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/apps/argos/stager/Checkpoint.hpp"

namespace simdb::argos {

//! \class CollectableCheckpointer
//! \brief Base class for all collectable checkpointers.
class CollectableCheckpointer
{
public:
    virtual ~CollectableCheckpointer() = default;

    virtual void createCheckpoint(uint64_t window_id, const std::vector<char>& scalar_bytes)
    {
        (void)window_id; (void)scalar_bytes;
        throw DBException("Not implemented");
    }

    virtual void createCheckpoint(uint64_t window_id, const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        (void)window_id; (void)contig_bin_bytes;
        throw DBException("Not implemented");
    }

    virtual void createCheckpoint(uint64_t window_id, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        (void)window_id; (void)sparse_bin_bytes;
        throw DBException("Not implemented");
    }

    virtual void onEnabledChanged(uint64_t window_id, bool enabled) = 0;

    virtual void onQuietChanged(uint64_t window_id, bool quiet) = 0;

    virtual std::unique_ptr<CollectedData> encodeForPipeline(uint64_t window_id, uint16_t cid) = 0;

    virtual void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager*) { (void)cid; }

protected:
    explicit CollectableCheckpointer(size_t heartbeat) : heartbeat_(heartbeat) {}
    const size_t heartbeat_;
};

//! \class ScalarCheckpointer
//! \brief Responsible for delta encoding for scalar types (including structs)
class ScalarCheckpointer final : public CollectableCheckpointer
{
public:
    ScalarCheckpointer(size_t heartbeat) : CollectableCheckpointer(heartbeat) {}

    void createCheckpoint(uint64_t window_id, const std::vector<char>& scalar_bytes) override
    {
        head_ = std::make_shared<ScalarCheckpoint>(head_, window_id, scalar_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onEnabledChanged(uint64_t window_id, bool enabled) override
    {
        head_ = std::make_shared<ScalarCheckpoint>(head_, window_id, enabled);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onQuietChanged(uint64_t window_id, bool quiet) override
    {
        onEnabledChanged(window_id, quiet);
    }

    std::unique_ptr<CollectedData> encodeForPipeline(uint64_t window_id, uint16_t cid) override
    {
        auto anchor = getAnchor_(window_id);
        assert(anchor != nullptr);
        assert(window_id >= anchor->getWindowID());
        auto force_snapshot = window_id - anchor->getWindowID() > heartbeat_;
        return encodeForPipeline_(anchor, cid, force_snapshot);
    }

private:
    ScalarCheckpoint* getAnchor_(uint64_t window_id)
    {
        ScalarCheckpoint* anchor = tail_.get();
        while (anchor)
        {
            if (anchor->getWindowID() > window_id)
            {
                anchor = anchor->prev();
                break;
            }
            anchor = anchor->next();
        }
        return anchor;
    }

    std::unique_ptr<CollectedData> encodeForPipeline_(ScalarCheckpoint* anchor, uint16_t cid, bool force_snapshot)
    {
        if (!anchor)
        {
            return nullptr;
        }
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        // TODO XXX: cleanup chain
        return encoded;
    }

    std::shared_ptr<ScalarCheckpoint> head_;
    std::shared_ptr<ScalarCheckpoint> tail_;
};

//! \class ContigContainerCheckpointer
//! \brief Responsible for delta encoding for contiguous containers
class ContigContainerCheckpointer final : public CollectableCheckpointer
{
public:
    ContigContainerCheckpointer(size_t heartbeat, size_t capacity)
        : CollectableCheckpointer(heartbeat)
        , capacity_(capacity)
    {}

    void createCheckpoint(uint64_t window_id, const std::vector<std::vector<char>>& contig_bin_bytes) override
    {
        max_container_size_ = std::max(max_container_size_, getSize_(contig_bin_bytes));
        assert(max_container_size_ <= capacity_);

        head_ = std::make_shared<ContigContainerCheckpoint>(head_, window_id, contig_bin_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onEnabledChanged(uint64_t window_id, bool enabled) override
    {
        head_ = std::make_shared<ContigContainerCheckpoint>(head_, window_id, enabled);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onQuietChanged(uint64_t window_id, bool quiet) override
    {
        onEnabledChanged(window_id, quiet);
    }

    std::unique_ptr<CollectedData> encodeForPipeline(uint64_t window_id, uint16_t cid) override
    {
        //TODO XXX:
        // Merge the logic from simdb/apps/argos/CheckpointDeltas.hpp (classifyContigChange)
        (void)window_id;
        (void)cid;
        return nullptr;
    }

    void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager* db_mgr) override
    {
        db_mgr->INSERT(
            SQL_TABLE("QueueMaxSizes"),
            SQL_VALUES((int)cid, (int)max_container_size_));
    }

private:
    static size_t getSize_(const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        size_t size = 0;
        for (const auto& bin_bytes : contig_bin_bytes)
        {
            if (!bin_bytes.empty())
            {
                ++size;
            } else
            {
                break;
            }
        }
        assert(size <= UINT16_MAX);
        return size;
    }

    std::shared_ptr<ContigContainerCheckpoint> head_;
    std::shared_ptr<ContigContainerCheckpoint> tail_;
    size_t max_container_size_ = 0;
    const size_t capacity_;
};

//! \class SparseContainerCheckpointer
//! \brief Responsible for delta encoding for sparse containers
class SparseContainerCheckpointer final : public CollectableCheckpointer
{
public:
    SparseContainerCheckpointer(size_t heartbeat, size_t capacity)
        : CollectableCheckpointer(heartbeat)
        , capacity_(capacity)
    {}

    void createCheckpoint(uint64_t window_id, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes) override
    {
        max_container_size_ = std::max(max_container_size_, getSize_(sparse_bin_bytes));
        assert(max_container_size_ <= capacity_);

        head_ = std::make_shared<SparseContainerCheckpoint>(head_, window_id, sparse_bin_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onEnabledChanged(uint64_t window_id, bool enabled) override
    {
        head_ = std::make_shared<SparseContainerCheckpoint>(head_, window_id, enabled);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onQuietChanged(uint64_t window_id, bool quiet) override
    {
        onEnabledChanged(window_id, quiet);
    }

    std::unique_ptr<CollectedData> encodeForPipeline(uint64_t window_id, uint16_t cid) override
    {
        //TODO XXX
        (void)window_id;
        (void)cid;
        return nullptr;
    }

    void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager* db_mgr) override
    {
        db_mgr->INSERT(
            SQL_TABLE("QueueMaxSizes"),
            SQL_VALUES((int)cid, (int)max_container_size_));
    }

private:
    static size_t getSize_(const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        size_t size = 0;
        for (const auto& [_, bin_bytes] : sparse_bin_bytes)
        {
            if (!bin_bytes.empty())
            {
                ++size;
            }
        }
        return size;
    }

    std::shared_ptr<SparseContainerCheckpoint> head_;
    std::shared_ptr<SparseContainerCheckpoint> tail_;
    size_t max_container_size_ = 0;
    const size_t capacity_;
};

} // namespace simdb::argos
