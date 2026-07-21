// clang-format off

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/Checkpoint.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/Compress.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace argos_test {

using simdb::argos::Action;

/// Minimal byte reader matching python/argos/viewer/model/data_deserializers.py ByteBuffer.
class ByteBuffer
{
public:
    explicit ByteBuffer(std::vector<char> data) :
        data_(std::move(data))
    {
    }

    bool done() const { return read_idx_ >= data_.size(); }

    size_t tell() const { return read_idx_; }

    std::vector<char> slice(size_t start, size_t end) const
    {
        if (end > data_.size() || start > end)
        {
            throw simdb::DBException("ByteBuffer slice out of range");
        }
        return std::vector<char>(data_.begin() + static_cast<std::ptrdiff_t>(start),
                                 data_.begin() + static_cast<std::ptrdiff_t>(end));
    }

    template <typename T>
    T read()
    {
        static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
        T val{};
        readBytes(&val, sizeof(T));
        return val;
    }

    void readBytes(void* out, size_t num_bytes)
    {
        if (read_idx_ + num_bytes > data_.size())
        {
            throw simdb::DBException("ByteBuffer read past end");
        }
        std::memcpy(out, data_.data() + read_idx_, num_bytes);
        read_idx_ += num_bytes;
    }

private:
    std::vector<char> data_;
    size_t read_idx_ = 0;
};

struct BlobContext
{
    uint16_t current_cid = 0;
    uint64_t current_tick = 0;
};

class BlobHandler
{
public:
    virtual ~BlobHandler() = default;

    virtual void handleScalarClosed(BlobContext&) {}
    virtual void handleScalarCarried(BlobContext&) {}
    virtual void handleScalarFullDump(BlobContext&, const std::vector<char>&) {}

    virtual void handleContigContainerClosed(BlobContext&) {}
    virtual void handleContigContainerCarried(BlobContext&) {}
    virtual void handleContigContainerFullDump(BlobContext&, const std::vector<std::vector<char>>&) {}
    virtual void handleContigContainerSwap(BlobContext&, uint16_t, const std::vector<char>&) {}
    virtual void handleContigContainerMultiSwap(BlobContext&, const std::vector<uint16_t>&,
                                                const std::vector<std::vector<char>>&) {}
    virtual void handleContigContainerArrival(BlobContext&, const std::vector<char>&) {}
    virtual void handleContigContainerDeparture(BlobContext&) {}
    virtual void handleContigContainerBookends(BlobContext&, const std::vector<char>&) {}
    virtual void handleContigContainerMimo(BlobContext&, uint8_t /*depart_count*/, uint8_t /*arrive_count*/,
                                           const std::vector<std::vector<char>>&) {}

    virtual void handleSparseContainerClosed(BlobContext&) {}
    virtual void handleSparseContainerCarried(BlobContext&) {}
    virtual void handleSparseContainerFullDump(BlobContext&, const std::map<uint16_t, std::vector<char>>&) {}
    virtual void handleSparseContainerExchangedBin(BlobContext&, uint16_t, const std::vector<char>&) {}
    virtual void handleSparseContainerMultiSwap(BlobContext&, const std::vector<uint16_t>&,
                                                const std::vector<std::vector<char>>&) {}
    virtual void handleSparseContainerRemovedBin(BlobContext&, uint16_t) {}
    virtual void handleSparseContainerAddedBin(BlobContext&, uint16_t, const std::vector<char>&) {}
    virtual void handleSparseContainerMultiRemovedBins(BlobContext&, const std::vector<uint16_t>&) {}

    virtual void snapshotTick(BlobContext&) {}
};

/// Collectable metadata loaded from CollectableTreeNodes (mirrors python SimHierarchy routing).
class CollectableRegistry
{
public:
    explicit CollectableRegistry(simdb::DatabaseManager* db_mgr)
    {
        auto query = db_mgr->createQuery("CollectableTreeNodes");
        int32_t cid = 0;
        std::string type_name;
        query->select("CID", cid);
        query->select("TypeName", type_name);

        auto results = query->getResultSet();
        while (results.getNextRecord())
        {
            const auto cid_u16 = static_cast<uint16_t>(cid);
            dtype_by_cid_[cid_u16] = type_name;
            if (type_name.find("_contig_") != std::string::npos || type_name.find("_sparse_") != std::string::npos)
            {
                container_cids_.insert(cid_u16);
                if (type_name.find("_sparse_") != std::string::npos)
                {
                    sparse_cids_.insert(cid_u16);
                }
            }
        }
    }

    bool isContainer(uint16_t cid) const { return container_cids_.count(cid) > 0; }

    bool isSparse(uint16_t cid) const { return sparse_cids_.count(cid) > 0; }

    const std::string& getTypeName(uint16_t cid) const
    {
        return dtype_by_cid_.at(cid);
    }

private:
    std::unordered_map<uint16_t, std::string> dtype_by_cid_;
    std::unordered_set<uint16_t> container_cids_;
    std::unordered_set<uint16_t> sparse_cids_;
};

struct BlobResources
{
    CollectableRegistry& registry;
    BlobHandler& handler;
    std::function<std::vector<char>(uint16_t cid, ByteBuffer&)> deserialize_bin;
    ByteBuffer* buf = nullptr;
};

/// Handler step returned by HandleCID / HandleAction / HandleScalarAction (python-style chaining).
struct HandlerStep
{
    using Fn = std::function<HandlerStep(BlobResources&, BlobContext&)>;
    Fn fn;

    HandlerStep() = default;

    explicit HandlerStep(Fn step_fn) :
        fn(std::move(step_fn))
    {
    }

    explicit operator bool() const { return static_cast<bool>(fn); }

    HandlerStep operator()(BlobResources& resources, BlobContext& context) const
    {
        return fn ? fn(resources, context) : HandlerStep{};
    }
};

HandlerStep handleAction(BlobResources& resources, BlobContext& context);
HandlerStep handleScalarAction(BlobResources& resources, BlobContext& context, uint8_t action);
HandlerStep handleContigContainerAction(BlobResources& resources, BlobContext& context, uint8_t action);
HandlerStep handleSparseContainerAction(BlobResources& resources, BlobContext& context, uint8_t action);

inline HandlerStep handleCID(BlobResources& resources, BlobContext& context)
{
    if (resources.buf->done())
    {
        return {};
    }

    context.current_cid = resources.buf->read<uint16_t>();
    return HandlerStep{handleAction};
}

inline HandlerStep handleAction(BlobResources& resources, BlobContext& context)
{
    const auto action = resources.buf->read<uint8_t>();

    if (!resources.registry.isContainer(context.current_cid))
    {
        return HandlerStep{[action](BlobResources& res, BlobContext& ctx) {
            return handleScalarAction(res, ctx, action);
        }};
    }

    if (resources.registry.isSparse(context.current_cid))
    {
        return HandlerStep{[action](BlobResources& res, BlobContext& ctx) {
            return handleSparseContainerAction(res, ctx, action);
        }};
    }

    return HandlerStep{[action](BlobResources& res, BlobContext& ctx) {
        return handleContigContainerAction(res, ctx, action);
    }};
}

inline HandlerStep handleScalarAction(BlobResources& resources, BlobContext& context, uint8_t action)
{
    switch (static_cast<Action>(action))
    {
    case Action::CLOSED:
        resources.handler.handleScalarClosed(context);
        break;
    case Action::CARRY:
        resources.handler.handleScalarCarried(context);
        break;
    default:
    {
        auto deserialize = [&](ByteBuffer& byte_buf) {
            return resources.deserialize_bin(context.current_cid, byte_buf);
        };

        switch (static_cast<Action>(action))
        {
        case Action::FULL:
            resources.handler.handleScalarFullDump(context, deserialize(*resources.buf));
            break;
        default:
            throw simdb::DBException("Unexpected scalar action byte: " + std::to_string(action));
        }
        break;
    }
    }

    return HandlerStep{handleCID};
}

inline HandlerStep handleContigContainerAction(BlobResources& resources, BlobContext& context, uint8_t action)
{
    switch (static_cast<Action>(action))
    {
    case Action::CLOSED:
        resources.handler.handleContigContainerClosed(context);
        break;
    case Action::CARRY:
        resources.handler.handleContigContainerCarried(context);
        break;
    case Action::FULL:
    {
        const auto size = resources.buf->read<uint16_t>();
        std::vector<std::vector<char>> bins;
        bins.reserve(size);
        for (uint16_t i = 0; i < size; ++i)
        {
            (void)i;
            bins.emplace_back(resources.deserialize_bin(context.current_cid, *resources.buf));
        }

        resources.handler.handleContigContainerFullDump(context, bins);
        break;
    }
    case Action::CONTAINER_SWAP:
    {
        const auto bin_idx = resources.buf->read<uint16_t>();
        auto payload = resources.deserialize_bin(context.current_cid, *resources.buf);
        resources.handler.handleContigContainerSwap(context, bin_idx, payload);
        break;
    }
    case Action::CONTAINER_MULTI_SWAP:
    {
        const auto count = resources.buf->read<uint8_t>();
        std::vector<uint16_t> indices;
        std::vector<std::vector<char>> payloads;
        indices.reserve(count);
        payloads.reserve(count);
        for (uint8_t i = 0; i < count; ++i)
        {
            (void)i;
            indices.push_back(resources.buf->read<uint16_t>());
            payloads.push_back(resources.deserialize_bin(context.current_cid, *resources.buf));
        }
        resources.handler.handleContigContainerMultiSwap(context, indices, payloads);
        break;
    }
    case Action::CONTIG_ARRIVE:
    {
        auto payload = resources.deserialize_bin(context.current_cid, *resources.buf);
        resources.handler.handleContigContainerArrival(context, payload);
        break;
    }
    case Action::CONTIG_DEPART:
        resources.handler.handleContigContainerDeparture(context);
        break;
    case Action::CONTIG_BOOKENDS:
    {
        auto payload = resources.deserialize_bin(context.current_cid, *resources.buf);
        resources.handler.handleContigContainerBookends(context, payload);
        break;
    }
    case Action::CONTIG_MIMO:
    {
        const auto depart_count = resources.buf->read<uint8_t>();
        const auto arrive_count = resources.buf->read<uint8_t>();
        std::vector<std::vector<char>> arrive_payloads;
        arrive_payloads.reserve(arrive_count);
        for (uint8_t i = 0; i < arrive_count; ++i)
        {
            (void)i;
            arrive_payloads.push_back(resources.deserialize_bin(context.current_cid, *resources.buf));
        }
        resources.handler.handleContigContainerMimo(context, depart_count, arrive_count, arrive_payloads);
        break;
    }
    default:
        throw simdb::DBException("Unexpected contig container action byte: " + std::to_string(action));
    }

    return HandlerStep{handleCID};
}

inline HandlerStep handleSparseContainerAction(BlobResources& resources, BlobContext& context, uint8_t action)
{
    switch (static_cast<Action>(action))
    {
    case Action::CLOSED:
        resources.handler.handleSparseContainerClosed(context);
        break;
    case Action::CARRY:
        resources.handler.handleSparseContainerCarried(context);
        break;
    case Action::FULL:
    {
        const auto size = resources.buf->read<uint16_t>();
        std::map<uint16_t, std::vector<char>> bins;
        for (uint16_t i = 0; i < size; ++i)
        {
            (void)i;
            const auto bin_idx = resources.buf->read<uint16_t>();
            bins.emplace(bin_idx, resources.deserialize_bin(context.current_cid, *resources.buf));
        }

        resources.handler.handleSparseContainerFullDump(context, bins);
        break;
    }
    case Action::CONTAINER_SWAP:
    {
        const auto bin_idx = resources.buf->read<uint16_t>();
        auto payload = resources.deserialize_bin(context.current_cid, *resources.buf);
        resources.handler.handleSparseContainerExchangedBin(context, bin_idx, payload);
        break;
    }
    case Action::CONTAINER_MULTI_SWAP:
    {
        const auto count = resources.buf->read<uint8_t>();
        std::vector<uint16_t> indices;
        std::vector<std::vector<char>> payloads;
        indices.reserve(count);
        payloads.reserve(count);
        for (uint8_t i = 0; i < count; ++i)
        {
            (void)i;
            indices.push_back(resources.buf->read<uint16_t>());
            payloads.push_back(resources.deserialize_bin(context.current_cid, *resources.buf));
        }
        resources.handler.handleSparseContainerMultiSwap(context, indices, payloads);
        break;
    }
    case Action::SPARSE_REMOVE:
    {
        const auto bin_idx = resources.buf->read<uint16_t>();
        resources.handler.handleSparseContainerRemovedBin(context, bin_idx);
        break;
    }
    case Action::SPARSE_ADD:
    {
        const auto bin_idx = resources.buf->read<uint16_t>();
        auto payload = resources.deserialize_bin(context.current_cid, *resources.buf);
        resources.handler.handleSparseContainerAddedBin(context, bin_idx, payload);
        break;
    }
    case Action::SPARSE_MULTI_REMOVE:
    {
        const auto count = resources.buf->read<uint8_t>();
        std::vector<uint16_t> indices;
        indices.reserve(count);
        for (uint8_t i = 0; i < count; ++i)
        {
            (void)i;
            indices.push_back(resources.buf->read<uint16_t>());
        }
        resources.handler.handleSparseContainerMultiRemovedBins(context, indices);
        break;
    }
    default:
        throw simdb::DBException("Unexpected sparse container action byte: " + std::to_string(action));
    }

    return HandlerStep{handleCID};
}

class BlobIterator
{
public:
    BlobIterator(simdb::DatabaseManager* db_mgr, CollectableRegistry& registry) :
        db_mgr_(db_mgr),
        registry_(registry)
    {
    }

    void iterate(BlobHandler& handler,
                 const std::function<std::vector<char>(uint16_t cid, ByteBuffer&)>& deserialize_bin)
    {
        auto timestamp_rows = loadTimestampRows_();
        auto blobs_by_ts_id = loadCollectionRecords_();

        BlobResources resources{registry_, handler, deserialize_bin, nullptr};
        BlobContext context;

        for (const auto& [ts_id, tick] : timestamp_rows)
        {
            const auto blob_iter = blobs_by_ts_id.find(ts_id);
            if (blob_iter == blobs_by_ts_id.end())
            {
                continue;
            }

            std::vector<char> uncompressed;
            simdb::decompressData(blob_iter->second, uncompressed);

            ByteBuffer buf(std::move(uncompressed));
            resources.buf = &buf;
            context.current_tick = tick;

            HandlerStep handler_step{handleCID};
            while (handler_step)
            {
                handler_step = handler_step(resources, context);
            }

            handler.snapshotTick(context);
        }
    }

private:
    std::vector<std::pair<int64_t, uint64_t>> loadTimestampRows_()
    {
        std::vector<std::pair<int64_t, uint64_t>> rows;
        auto query = db_mgr_->createQuery("Timestamps");

        int64_t ts_id = 0;
        uint64_t tick = 0;
        query->select("Id", ts_id);
        query->select("Timestamp", tick);

        auto results = query->getResultSet();
        while (results.getNextRecord())
        {
            rows.emplace_back(ts_id, tick);
        }
        return rows;
    }

    std::map<int64_t, std::vector<char>> loadCollectionRecords_()
    {
        std::map<int64_t, std::vector<char>> blobs;
        auto query = db_mgr_->createQuery("CollectionRecords");

        int64_t timestamp_id = 0;
        std::vector<char> records;
        query->select("TimestampID", timestamp_id);
        query->select("Records", records);

        auto results = query->getResultSet();
        while (results.getNextRecord())
        {
            blobs.emplace(timestamp_id, records);
        }
        return blobs;
    }

    simdb::DatabaseManager* db_mgr_;
    CollectableRegistry& registry_;
};

} // namespace argos_test
