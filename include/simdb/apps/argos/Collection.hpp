// <Collection.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectionBase.hpp"
#include "simdb/apps/argos/DomainCollection.hpp"
#include "simdb/apps/argos/CollectionPipeline.hpp"
#include "simdb/apps/argos/Minifiers.hpp"
#include "simdb/apps/argos/DataTypeInspector.hpp"
#include "simdb/apps/argos/DataTypeSerializer.hpp"
#include "simdb/utils/CollectionByteTrace.hpp"
#include "simdb/utils/Tree.hpp"
#include "simdb/utils/TypeTraits.hpp"
#include "simdb/Exceptions.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace simdb::collection {

namespace detail {

/// Strip smart/raw pointer layers so container \c value_types like \c std::shared_ptr<T>
/// or other \c is_any_pointer types register the same schema as the pointee (mirrors
/// \c ContainerCollector::ValueType).
template <typename T, bool = type_traits::is_any_pointer_v<T>>
struct dtype_register_element_impl
{
    using type = T;
};

template <typename T>
struct dtype_register_element_impl<T, true>
{
    using type = typename dtype_register_element_impl<
        type_traits::remove_any_pointer_t<T>>::type;
};

template <typename T>
struct dtype_register_element
{
    using bare = std::remove_cv_t<std::remove_reference_t<T>>;
    using type = typename dtype_register_element_impl<bare>::type;
};

} // namespace detail

constexpr inline size_t DEFAULT_HEARTBEAT = 10;

/// \class Collection
/// \brief Holds one \ref TimeT for all clock domains and a \ref TimeDomainCollection per clock.
template <typename TimeT> class Collection : public CollectionBase
{
public:
    /// \brief Construct
    /// \param heartbeat Size/speed tunable parameter; gives the max number
    /// of times a collectable can use its disk space optimization strategy,
    /// at which point its data must be forcible captured. Higher heartbeats
    /// lead to smaller databases but the Argos UI will be less responsive.
    /// \param enabled_paths If given, only the collectables at these paths
    /// will be collected by default. Other collectables that were created
    /// outside this list will have to be explicitly enabled during simulation
    /// via the CollectableBase::enable() method.
    Collection(size_t heartbeat = DEFAULT_HEARTBEAT,
               const std::vector<std::string>& enabled_paths = {})
        : heartbeat_(heartbeat)
        , default_enabled_paths_(enabled_paths)
    {
        if (heartbeat_ == 0)
        {
            throw DBException("Cannot use value 0 for collection heartbeat");
        }
    }

    /// \brief Use a backpointer to get the current time for every clock domain.
    /// \throw DBException if any clock domain has already been added via addCollection().
    void timestampWith(const TimeT* backpointer)
    {
        ensureTimestampReconfigurable_();
        timestamp_ = std::make_shared<Timestamp<TimeT>>(backpointer);
    }

    /// \brief Use a C-style function pointer to get the current time for every clock domain.
    /// \throw DBException if any clock domain has already been added via addCollection().
    void timestampWith(TimeT (*fn)())
    {
        ensureTimestampReconfigurable_();
        timestamp_ = std::make_shared<Timestamp<TimeT>>(fn);
    }

    /// \brief Use a \c std::function to get the current time for every clock domain.
    /// \throw DBException if any clock domain has already been added via addCollection().
    void timestampWith(std::function<TimeT()> fn)
    {
        ensureTimestampReconfigurable_();
        timestamp_ = std::make_shared<Timestamp<TimeT>>(std::move(fn));
    }

    /// \brief Return the collection heartbeat
    size_t getHeartbeat() const override
    {
        return heartbeat_;
    }

    /// \brief Called when the app is created
    SqlDataType getSqlTimeType() const override
    {
        if constexpr (std::is_same_v<TimeT, uint64_t>)
        {
            return SqlDataType::uint64_t;
        }
        else if constexpr (std::is_same_v<TimeT, int64_t>)
        {
            return SqlDataType::int64_t;
        }
        else if constexpr (std::is_same_v<TimeT, uint32_t>)
        {
            return SqlDataType::uint32_t;
        }
        else if constexpr (std::is_same_v<TimeT, int32_t>)
        {
            return SqlDataType::int32_t;
        }
        else if constexpr (std::is_floating_point_v<TimeT>)
        {
            return SqlDataType::double_t;
        }
        else
        {
            static_assert(std::is_integral_v<TimeT>);
            static_assert(std::is_unsigned_v<TimeT>);
            static_assert(sizeof(TimeT) <= sizeof(int32_t));
            return SqlDataType::int32_t;
        }
    }

    /// \brief Add a collection for one clock domain
    /// \param clk_name Clock name
    /// \param clk_period Clock period
    /// \throw Throws if this clock already had a collection, but with a different
    /// clock period
    void addCollection(const std::string& clk_name, size_t clk_period)
    {
        if (clk_periods_.count(clk_name) && clk_periods_[clk_name] != clk_period)
        {
            throw DBException("Cannot add collection for clock '")
                << clk_name << "' with period " << clk_period << ". This clock "
                << "already has a collection with period " << clk_periods_[clk_name];
        }

        if (!timestamp_)
        {
            throw DBException("Must call timestampWith() before addCollection()");
        }

        auto& collection = collections_[clk_name];
        if (!collection)
        {
            collection = std::make_unique<TimeDomainCollection<TimeT>>(timestamp_, this);
            clk_periods_[clk_name] = clk_period;
        }
    }

    /// \brief Create a collectable for a scalar (integral/floating-point/enum/string/bool, or a
    /// struct-like object containing these types). Supports auto-collection and manual collection.
    /// \param path Dot-delimited path to the scalar that uniquely defines where this variable
    /// lives in the simulator.
    /// \param clk_name Name of the clock this collection point belongs to. Must have already
    /// called addCollection() with this clock name.
    /// \throw Throws if collection does not exist for the given clock.
    template <typename CollectableT>
    std::shared_ptr<AutoScalarCollector<CollectableT>> collectScalarWithAutoCollection(
        const std::string& path,
        const std::string& clk_name,
        const CollectableT* scalar)
    {
        verifyNoDupPaths_(path);
        using ElemT = type_traits::remove_any_pointer_t<CollectableT>;
        auto dtype_hier = dtype_inspector_.registerType<ElemT>();
        auto collection = getCollection_(clk_name, true /*must exist*/);
        auto collectable = std::make_shared<AutoScalarCollector<CollectableT>>(
            collection, heartbeat_, std::move(dtype_hier), scalar);
        collection->addCollectable(path, collectable, true /*auto collect*/);
        return collectable;
    }

    /// \brief Same as autoCollectScalar, but only supports manual collection.
    template <typename CollectableT>
    std::shared_ptr<ScalarCollector<CollectableT>> collectScalarManually(
        const std::string& path,
        const std::string& clk_name)
    {
        verifyNoDupPaths_(path);
        using ElemT = type_traits::remove_any_pointer_t<CollectableT>;
        auto dtype_hier = dtype_inspector_.registerType<ElemT>();
        auto collection = getCollection_(clk_name, true /*must exist*/);
        auto collectable =
            std::make_shared<ScalarCollector<CollectableT>>(collection, heartbeat_,
                std::move(dtype_hier));
        collection->addCollectable(path, collectable, false /*manually collect*/);
        return collectable;
    }

    /// \brief Create a collectable for a queue-like data structure, where ValueType is
    /// either integral, floating-point, enum, string, bool, or a struct-like object
    /// containing these types.
    template <typename ContainerT, bool Sparse>
    std::shared_ptr<AutoContainerCollector<ContainerT, Sparse>> collectContainerWithAutoCollection(
        const std::string& path,
        const std::string& clk_name,
        const ContainerT* container,
        size_t expected_capacity)
    {
        verifyNoDupPaths_(path);
        using InnerContainerT = type_traits::remove_any_pointer_t<ContainerT>;
        using ElemT =
            typename detail::dtype_register_element<typename InnerContainerT::value_type>::type;
        auto dtype_hier = dtype_inspector_.registerType<ElemT>();
        auto collection = getCollection_(clk_name, true /*must exist*/);
        auto collectable = std::make_shared<AutoContainerCollector<ContainerT, Sparse>>(
            collection, heartbeat_, container, expected_capacity, std::move(dtype_hier));
        collection->addCollectable(path, collectable, true /*auto collect*/);
        return collectable;
    }

    /// \brief Same as autoCollectQueue, but only supports manual collection.
    template <typename ContainerT, bool Sparse>
    std::shared_ptr<ContainerCollector<ContainerT, Sparse>> collectContainerManually(
        const std::string& path,
        const std::string& clk_name,
        size_t expected_capacity)
    {
        verifyNoDupPaths_(path);
        using InnerContainerT = type_traits::remove_any_pointer_t<ContainerT>;
        using ElemT =
            typename detail::dtype_register_element<typename InnerContainerT::value_type>::type;
        auto dtype_hier = dtype_inspector_.registerType<ElemT>();
        auto collection = getCollection_(clk_name, true /*must exist*/);
        auto collectable = std::make_shared<ContainerCollector<ContainerT, Sparse>>(
            collection, heartbeat_, expected_capacity, std::move(dtype_hier));
        collection->addCollectable(path, collectable, false /*manually collect*/);
        return collectable;
    }

    /// \brief Connect the collectables to the CollectorPipeline's main input queue
    void connectToPipeline(ConcurrentQueue<QueueCollectionData>* pipeline_head) override
    {
        for (auto& [_, collection] : collections_)
        {
            collection->connectToPipeline(pipeline_head);
        }
    }

    /// \brief Get the TinyStrings object used to map strings to ints in the DB
    TinyStrings<>* getTinyStrings() const override
    {
        return dtype_inspector_.getTinyStrings();
    }

    using CollectionBase::enableByteTracer;

    /// \brief Enable per-thread collection byte tracing to \p path for this collection's lifetime
    /// (replaces any previous tracer). Tracing is active on the thread that calls this until the
    /// \c Collection is destroyed or \ref enableByteTracer is called again.
    void enableByteTracer(const std::string& path, bool reopen_mode) override
    {
        if (tracer_ && tracer_->getTraceFile() == path)
        {
            return;
        }
        else if (tracer_)
        {
            std::cout << "WARNING: Ignoring request to redirect SimDB collection trace from '"
                      << tracer_->getTraceFile() << "' to '" << path << "'" << std::endl;
        }
        else
        {
            tracer_ = std::make_unique<simdb::utils::CollectionByteTraceSession>(path, reopen_mode);
            byte_trace_path_for_meta_ = path;
        }
    }

    /// \brief Run auto-collection on all collectables configured for it
    void performAutoCollection(const std::string& clk_name, bool send_to_pipeline = false)
    {
        auto collection = getCollection_(clk_name, true /*must exist*/);
        collection->performAutoCollection();
        if (send_to_pipeline)
        {
            collection->sendCollectedDataToPipeline();
        }
    }

    /// \brief Calls to performAutoCollection(), just like all explicit
    /// calls to the collect() methods, only collect the data bytes and
    /// organize them by their timestamps. You must call this method
    /// to push the data down the pipeline.
    void sendCollectedDataToPipeline(const std::string& clk_name)
    {
        auto collection = getCollection_(clk_name, true /*must exist*/);
        collection->sendCollectedDataToPipeline();
    }

    bool minifiersSawAllActions() const override
    {
        for (const auto& [_, collection] : collections_)
        {
            if (!collection->minifiersSawAllActions())
            {
                return false;
            }
        }
        return true;
    }

private:
    /// \brief Verify that all collectables are uniquely owned by clock-specific
    /// \ref TimeDomainCollection instances
    void verifyNoDupPaths_(const std::string& path)
    {
        if (!all_collectable_paths_.insert(path).second)
        {
            throw DBException("Collectable already exists at path: ") << path;
        }
    }

    /// \brief Ensure we can still create the timestamp; once domain collections
    /// are added we can no longer change the timestamp
    void ensureTimestampReconfigurable_() const
    {
        if (!collections_.empty())
        {
            throw DBException("timestampWith() cannot be called after addCollection()");
        }
    }

    /// Write \c \<trace_path>.meta alongside the trace: per-CID paths, types, container info, manual vs auto API.
    void writeCollectionTraceMeta_(const std::string& trace_path) const
    {
        struct CollectableMetaRow
        {
            uint16_t cid = 0;
            std::string clock;
            std::string path;
            const CollectableBase* collectable = nullptr;
        };

        std::vector<CollectableMetaRow> rows;
        for (const auto& [clk_name, collection] : collections_)
        {
            if (!collection)
            {
                continue;
            }
            for (const auto& collectable_path : collection->getCollectablePaths())
            {
                auto* coll_base = collection->getCollectable(collectable_path);
                rows.push_back({coll_base->getID(), clk_name, collectable_path, coll_base});
            }
        }

        std::sort(
            rows.begin(),
            rows.end(),
            [](const CollectableMetaRow& a, const CollectableMetaRow& b) { return a.cid < b.cid; });

        const std::string meta_path = trace_path + ".meta";
        std::ofstream meta(meta_path, std::ios::out | std::ios::trunc);
        if (!meta.good())
        {
            std::cout << "WARNING: Could not write collection trace metadata file '" << meta_path << "'" << std::endl;
            return;
        }

        meta << "# SimDB collection byte trace metadata (CID index for matching .trace to collectables)\n";
        meta << "trace_file: " << trace_path << "\n";
        meta << "heartbeat: " << heartbeat_ << "\n\n";

        for (const auto& r : rows)
        {
            const auto* c = r.collectable;
            meta << "--- CID: " << static_cast<unsigned>(r.cid) << " ---\n";
            meta << "full_path: " << r.path << "\n";
            meta << "clock: " << r.clock << "\n";
            meta << "collection_registration: "
                 << (c->traceRegisteredAsAutoCollectApi() ? "auto-collect-api" : "manual-collect-api") << "\n";
            if (c->traceIsContainer())
            {
                meta << "kind: " << (c->traceIsSparseContainer() ? "sparse-container" : "contiguous-container")
                     << "\n";
                meta << "expected_capacity: " << c->traceExpectedCapacity() << "\n";
            }
            else
            {
                meta << "kind: scalar\n";
            }
            const auto root_bytes = c->traceSerializationRootTypeBytes();
            meta << "serialization_root_type: " << c->traceSerializationRootType() << " (" << root_bytes
                 << (root_bytes == 1 ? " byte)\n" : " bytes)\n");
            meta << '\n';
        }
    }

    /// \brief Return the collection for the given clock
    /// \throw Throws if not found and must_exist=true
    DomainCollection* getCollection_(const std::string& clk_name, bool must_exist = false) const
    {
        auto it = collections_.find(clk_name);
        if (it == collections_.end())
        {
            if (must_exist)
            {
                throw DBException("Collection does not exist for clock: ") << clk_name;
            }
            return nullptr;
        }

        return it->second.get();
    }

    /// \brief Called when handling the app's postInit()
    void writeMetaOnPostInit(DatabaseManager* db_mgr) override
    {
        // Write heartbeat
        db_mgr->INSERT(SQL_TABLE("CollectionGlobals"), SQL_VALUES(heartbeat_));

        db_mgr->safeTransaction([&]() {
            std::map<std::string, int> clock_db_ids;
            for (const auto& [clk_name, period] : clk_periods_)
            {
                auto coll_it = collections_.find(clk_name);
                if (coll_it == collections_.end() || !coll_it->second)
                {
                    continue;
                }
                auto clk_rec = db_mgr->INSERT(
                    SQL_TABLE("Clocks"),
                    SQL_VALUES(clk_name, static_cast<int32_t>(period)));
                clock_db_ids[clk_name] = clk_rec->getId();
            }

            for (const auto& [clk_name, collection] : collections_)
            {
                const int clock_id = clock_db_ids.at(clk_name);
                for (const auto& path : collection->getCollectablePaths())
                {
                    const auto* coll = collection->getCollectable(path);
                    db_mgr->INSERT(
                        SQL_TABLE("CollectableTreeNodes"),
                        SQL_VALUES(
                            (uint32_t)coll->getID(),
                            path,
                            clock_id,
                            coll->collectableTypeNameForDb()));
                }
            }
        });

        dtype_inspector_.bindDatabase(db_mgr);

        // Write data types and their hierarchies (structs / nested structs)
        DataTypeSerializer::serialize(&dtype_inspector_, db_mgr);

        if (!byte_trace_path_for_meta_.empty())
        {
            writeCollectionTraceMeta_(byte_trace_path_for_meta_);
        }
    }

    /// \brief Tell the PipelineStager to flush all pending collected
    /// data to the pipeline on preTeardown()
    void sendCollectedDataToPipeline() override
    {
        for (const auto& [clk_name, collection] : collections_)
        {
            collection->sendCollectedDataToPipeline();
        }
    }

    /// \brief Called when handling the app's postTeardown()
    void writeMetaOnPostTeardown(DatabaseManager* db_mgr) override
    {
        std::map<uint16_t, uint16_t> container_max_sizes;
        for (const auto& [clk_name, collection] : collections_)
        {
            for (auto collectable : collection->getAllCollectables())
            {
                if (auto container_collector = dynamic_cast<const ContainerCollectorBase*>(collectable))
                {
                    container_max_sizes[collectable->getID()] = container_collector->getMaxContainerSizeCollected();
                }
            }
        }

        auto inserter = db_mgr->prepareINSERT(SQL_TABLE("QueueMaxSizes"));
        for (const auto& [cid, sz] : container_max_sizes)
        {
            inserter->createRecordWithColValues(cid, sz);
        }
    }

    const size_t heartbeat_;
    const std::vector<std::string> default_enabled_paths_;
    std::map<std::string, std::unique_ptr<TimeDomainCollection<TimeT>>> collections_;
    std::map<std::string, size_t> clk_periods_;
    std::unordered_set<std::string> all_collectable_paths_;
    DataTypeInspector dtype_inspector_;
    std::shared_ptr<Timestamp<TimeT>> timestamp_;
    std::unique_ptr<PipelineStager<TimeT>> stager_;
    std::unique_ptr<simdb::utils::CollectionByteTraceSession> tracer_;
    /// When byte tracing is enabled, write \<path>.meta in \ref writeMetaOnPostInit (paths exist by then).
    std::string byte_trace_path_for_meta_;
};

} // namespace simdb::collection
