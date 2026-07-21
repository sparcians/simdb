// <ArgosCollector.hpp> -*- C++ *-*

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/apps/App.hpp"
#include "simdb/apps/argos/Checkpointer.hpp"
#include "simdb/apps/argos/EntryPoint.hpp"
#include "simdb/apps/argos/EnumInspector.hpp"
#include "simdb/apps/argos/PipelineStagerInterface.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/pipeline/PipelineManager.hpp"
#include "simdb/sqlite/Dump.hpp"
#include "simdb/utils/Compress.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"
#include "simdb/utils/TinyStrings.hpp"

namespace simdb::argos {

inline constexpr size_t DEFAULT_HEARTBEAT = 10;

//! \class ArgosCollector
//! \brief Main entry point into the Argos collection system.
class ArgosCollector : public App, public PipelineStagerInterface
{
public:
    //! Required by all SimDB apps
    static constexpr auto NAME = "argos-collector";

    ArgosCollector(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
    }

    static void defineSchema(Schema& schema)
    {
        using dt = SqlDataType;

        auto& globals_tbl = schema.addTable("CollectionGlobals");
        globals_tbl.addColumn("Heartbeat", dt::int32_t);

        auto& clks_tbl = schema.addTable("Clocks");
        clks_tbl.addColumn("Name", dt::string_t);
        clks_tbl.addColumn("Period", dt::uint32_t);
        clks_tbl.addColumn("Numer", dt::uint32_t);
        clks_tbl.addColumn("Denom", dt::uint32_t);
        clks_tbl.setColumnDefaultValue("Numer", 0);
        clks_tbl.setColumnDefaultValue("Denom", 0);

        auto& collectable_tns_tbl = schema.addTable("CollectableTreeNodes");
        collectable_tns_tbl.addColumn("CID", dt::int32_t);
        collectable_tns_tbl.addColumn("FullPath", dt::string_t);
        collectable_tns_tbl.addColumn("ClockID", dt::int32_t);
        collectable_tns_tbl.addColumn("TypeName", dt::string_t);
        collectable_tns_tbl.addColumn("ShowInUI", dt::int32_t);
        collectable_tns_tbl.setColumnDefaultValue("ShowInUI", 0);
        collectable_tns_tbl.ensureUnique("CID");
        collectable_tns_tbl.createIndexOn("CID");
        collectable_tns_tbl.unsetPrimaryKey();

        auto& enum_itypes_tbl = schema.addTable("CollectedEnums");
        enum_itypes_tbl.addColumn("EnumName", dt::string_t);
        enum_itypes_tbl.addColumn("EnumIntTypeName", dt::string_t);

        auto& enum_members_tbl = schema.addTable("EnumMembers");
        enum_members_tbl.addColumn("EnumID", dt::int32_t);
        enum_members_tbl.addColumn("MemberName", dt::string_t);
        enum_members_tbl.addColumn("MemberValueStr", dt::string_t);

        auto& timestamps_tbl = schema.addTable("Timestamps");
        timestamps_tbl.addColumn("Timestamp", dt::uint64_t);
        timestamps_tbl.ensureUnique("Timestamp");

        auto& collection_records_tbl = schema.addTable("CollectionRecords");
        collection_records_tbl.addColumn("TimestampID", dt::int32_t);
        collection_records_tbl.addColumn("Records", dt::blob_t);
        collection_records_tbl.ensureUnique("TimestampID");
        collection_records_tbl.unsetPrimaryKey();

        auto& timestamp_clocks_tbl = schema.addTable("TimestampClocks");
        timestamp_clocks_tbl.addColumn("TimestampID", dt::int32_t);
        timestamp_clocks_tbl.addColumn("ClockID", dt::int32_t);
        timestamp_clocks_tbl.createCompoundIndexOn({"TimestampID", "ClockID"});
        timestamp_clocks_tbl.unsetPrimaryKey();

        auto& queue_max_sizes_tbl = schema.addTable("QueueMaxSizes");
        queue_max_sizes_tbl.addColumn("CID", dt::int32_t);
        queue_max_sizes_tbl.addColumn("MaxSize", dt::int32_t);
        queue_max_sizes_tbl.ensureUnique("CID");
        queue_max_sizes_tbl.unsetPrimaryKey();

        auto& notif_tbl = schema.addTable("Notifications");
        notif_tbl.addColumn("Timestamp", dt::uint64_t);
        notif_tbl.addColumn("NotifType", dt::int32_t);
        notif_tbl.addColumn("NotifStr", dt::string_t);

        // TODO cnyce: populate this table in SimDB (Sparta will handle it for now)
        auto& dtype_schemas_tbl = schema.addTable("DataTypeSchemas");
        dtype_schemas_tbl.addColumn("RootTypeName", dt::string_t);

        // TODO cnyce: populate this table in SimDB (Sparta will handle it for now)
        auto& dtype_nodes_tbl = schema.addTable("DataTypeNodes");
        dtype_nodes_tbl.addColumn("SchemaId", dt::int32_t);
        dtype_nodes_tbl.addColumn("Name", dt::string_t);
        dtype_nodes_tbl.addColumn("TypeName", dt::string_t);
        dtype_nodes_tbl.addColumn("FormatStr", dt::string_t);
    }

    void setHeartbeat(size_t heartbeat)
    {
        simdb_assert(heartbeat != 0, "Cannot use 0 for Argos collector heartbeat");
        heartbeat_ = heartbeat;
    }

    void addClock(const std::string& clk_name, size_t period) { addClock(clk_name, period, 0, 0); }

    void addClock(const std::string& clk_name, size_t period, size_t numer, size_t denom)
    {
        for (const auto& [_clk_name, _period, _numer, _denom] : clocks_)
        {
            if (clk_name == _clk_name)
            {
                simdb_assert(period == _period && numer == _numer && denom == _denom,
                             "Clock mismatch - already registered with different params: " << clk_name);
            }
        }

        auto clk_desc = std::make_tuple(clk_name, period, numer, denom);
        clocks_.emplace_back(std::move(clk_desc));
    }

    void timestampWith(const uint64_t* backpointer)
    {
        simdb_assert(timestamp_ == nullptr, "Cannot change timestamp object once created!");
        timestamp_ = std::make_unique<Timestamp>(backpointer);
    }

    void timestampWith(uint64_t (*fn)())
    {
        simdb_assert(timestamp_ == nullptr, "Cannot change timestamp object once created!");
        timestamp_ = std::make_unique<Timestamp>(fn);
    }

    void timestampWith(std::function<uint64_t()> fn)
    {
        simdb_assert(timestamp_ == nullptr, "Cannot change timestamp object once created!");
        timestamp_ = std::make_unique<Timestamp>(fn);
    }

    //! TODO cnyce: Once the collection code from Sparta is moved to SimDB, change this
    //! to a template method so we can figure out the encoded data type name ourselves.
    //! Scalar types are encoded as follows:
    //!
    //!   For scalar PODs:
    //!   typeid(T).name()
    //!       "bool"
    //!       "unsigned long"
    //!       ...
    //!
    //!   For scalar enums with operator<< (defn held in separate table for string-int map):
    //!   typeid(T).name()
    //!       "IssueType"
    //!       "MMUState"
    //!       ...
    //!   Enum values in collection blobs are serialized as std::underlying_type_t<T>.
    //!
    //!   For scalar enums without operator<< (treated just like int PODs):
    //!   typeid(std::underlying_type_t<T>).name()
    //!       "int"
    //!       "unsigned int"
    //!       ...
    //!
    //!   For scalar struct-like types:
    //!   typeid(T).name()
    //!       "Packet"
    //!       "Inst"
    //!       ...
    //!
    //!   For scalar string-like types (std::string, const char*):
    //!       "string"
    EntryPoint* createScalarCollector(const std::string& path, const std::string& clk_name,
                                      const std::string& encoded_scalar_type)
    {
        auto entry_point = std::make_unique<EntryPoint>(this, &tiny_strings_);
        meta_by_cid_[entry_point->getID()] = std::make_tuple(path, clk_name, encoded_scalar_type);
        entry_points_.emplace_back(std::move(entry_point));
        return entry_points_.back().get();
    }

    //! TODO cnyce: Once the collection code from Sparta is moved to SimDB, change this
    //! to a template method so we can figure out the encoded data type name ourselves.
    //! Container types are encoded as follows:
    //!
    //!   <encoded_scalar_type>_<sparse/contig>_capacity<N>
    //!       "Inst_sparse_capacity32"
    //!       "bool_contig_capacity4"
    //!       ...
    EntryPoint* createContainerCollector(const std::string& path, const std::string& clk_name,
                                         const std::string& encoded_container_type)
    {
        auto entry_point = std::make_unique<EntryPoint>(this, &tiny_strings_);
        meta_by_cid_[entry_point->getID()] = std::make_tuple(path, clk_name, encoded_container_type);
        entry_points_.emplace_back(std::move(entry_point));
        return entry_points_.back().get();
    }

    TinyStrings<>* getTinyStrings() { return &tiny_strings_; }

    EnumInspector* getEnumInspector() { return &enum_inspector_; }

    void createPipeline(pipeline::PipelineManager* pipeline_mgr) override
    {
        auto pipeline = pipeline_mgr->createPipeline(NAME, this);

        pipeline_stager_ = pipeline->addStage<PipelineStager>("stager", heartbeat_);
        pipeline->addStage<Compressor>("compressor");
        pipeline->addStage<Writer>("writer");
        pipeline->noMoreStages();

        pipeline->bind("stager.data_output_queue", "compressor.data_input_queue");
        pipeline->bind("compressor.data_output_queue", "writer.data_input_queue");
        pipeline->noMoreBindings();

        pipeline_head_ = pipeline->getInPortQueue<LedgerPtr>("stager.main_input_queue");
        notif_head_ = pipeline->getInPortQueue<NotifEntry>("writer.notif_input_queue");

        for (const auto& [cid, meta] : meta_by_cid_)
        {
            const auto& encoded_dtype = std::get<2>(meta);

            ContainerMeta container_meta;
            if (extractContainerMeta_(encoded_dtype, container_meta))
            {
                pipeline_stager_->setContainerDataType(cid, container_meta.sparse, container_meta.capacity);
            } else
            {
                pipeline_stager_->setScalarDataType(cid);
            }
        }

        NotifEntry notif_entry;
        while (pending_notif_entries_.try_pop(notif_entry))
        {
            notif_head_->emplace(std::move(notif_entry));
        }

        for (const auto& collector : entry_points_)
        {
            auto cid = collector->getID();
            const auto& clk_name = std::get<1>(meta_by_cid_.at(cid));
            pipeline_stager_->setCollectableClock(cid, getClockId_(clk_name));
        }

        is_live_ = true;
    }

    void postInit(int, char**) override
    {
        db_mgr_->INSERT(SQL_TABLE("CollectionGlobals"), SQL_VALUES(heartbeat_));

        std::map<std::string, int> clk_ids;
        auto clk_inserter = db_mgr_->prepareINSERT(SQL_TABLE("Clocks"));
        for (const auto& [_clk_name, _period, _numer, _denom] : clocks_)
        {
            auto id = clk_inserter->createRecordWithColValues(_clk_name, _period, _numer, _denom);
            clk_ids[_clk_name] = id;
        }

        auto ctn_inserter = db_mgr_->prepareINSERT(SQL_TABLE("CollectableTreeNodes"));
        for (const auto& collector : entry_points_)
        {
            auto cid = (int)collector->getID();
            const auto& full_path = std::get<0>(meta_by_cid_.at(cid));
            const auto& clk_name = std::get<1>(meta_by_cid_.at(cid));
            const auto& dtype_name = std::get<2>(meta_by_cid_.at(cid));
            const auto clk_id = clk_ids.at(clk_name);
            ctn_inserter->createRecordWithColValues(cid, full_path, clk_id, dtype_name);
        }
    }

    void stage(uint16_t cid, std::vector<char>&& scalar_bytes) override
    {
        assertLive_();
        checkTimeAdvanced_();
        ledger_->recordScalar(cid, std::move(scalar_bytes));
    }

    void stage(uint16_t cid, std::vector<std::vector<char>>&& contig_bin_bytes) override
    {
        assertLive_();
        checkTimeAdvanced_();
        ledger_->recordContig(cid, std::move(contig_bin_bytes));
    }

    void stage(uint16_t cid, std::map<uint16_t, std::vector<char>>&& sparse_bin_bytes) override
    {
        assertLive_();
        checkTimeAdvanced_();
        ledger_->recordSparse(cid, std::move(sparse_bin_bytes));
    }

    void closeRecord(uint16_t cid) override
    {
        assertLive_();
        checkTimeAdvanced_();
        ledger_->closeRecord(cid);
    }

    void postNotif(const std::string& notif, NotifType type) override
    {
        NotifEntry entry(timestamp_.get(), notif, type);
        auto notif_entries = is_live_ ? notif_head_ : &pending_notif_entries_;
        notif_entries->emplace(std::move(entry));
    }

    void preTeardown() override
    {
        if (ledger_ && ledger_->hasEntries())
        {
            pipeline_head_->emplace(std::move(ledger_));
        }
    }

    void postTeardown() override
    {
        if (pipeline_stager_)
        {
            pipeline_stager_->writeMetaOnPostTeardown(db_mgr_);
        }
        tiny_strings_.serialize(db_mgr_);
        enum_inspector_.serializeEnumMaps(db_mgr_);

        if (verbose())
        {
            std::cout << "[simdb] Collection tables at the end of simulation (except timestamps/blobs):\n\n";
            dumpTable(db_mgr_, "CollectionGlobals");
            dumpTable(db_mgr_, "Clocks");
            dumpTable(db_mgr_, "CollectedEnums");
            dumpTable(db_mgr_, "EnumMembers");
            dumpTable(db_mgr_, "CollectableTreeNodes");
            dumpTable(db_mgr_, "QueueMaxSizes");
            dumpTable(db_mgr_, "Notifications");
        }
    }

private:
    /// Entry to the DB pipeline (1st async stage input)
    ConcurrentQueue<LedgerPtr>* pipeline_head_ = nullptr;

    /// Entry to the DB writer stage's notifications
    ConcurrentQueue<NotifEntry>* notif_head_ = nullptr;

    /// Queued notifications prior to createPipeline()
    ConcurrentQueue<NotifEntry> pending_notif_entries_;

    /// Ledger of all collection activity at one simulation time point
    LedgerPtr ledger_;

    /// \class PipelineStager
    /// \brief 1st async stage. Takes collected data from the Ledger and appends checkpoints to the
    /// CID's checkpoint chains for snapshot-delta compression.
    class PipelineStager : public pipeline::Stage
    {
    public:
        PipelineStager(size_t heartbeat) :
            heartbeat_(heartbeat)
        {
            addInPort_<LedgerPtr>("main_input_queue", main_input_queue_);
            addOutPort_<CollectionEntries>("data_output_queue", data_output_queue_);
        }

        /// Must be called from main thread
        void setCollectableClock(uint16_t cid, uint32_t clock_id)
        {
            collectable_clock_ids_[cid] = clock_id;
            clock_ids_.insert(clock_id);
        }

        /// Must be called from main thread
        void setScalarDataType(uint16_t cid)
        {
            checkpointers_[cid] = std::make_unique<ScalarCheckpointer>(heartbeat_);
            ++num_scalars_;
        }

        /// Must be called from main thread
        void setContainerDataType(uint16_t cid, bool sparse, size_t capacity)
        {
            if (sparse)
            {
                checkpointers_[cid] = std::make_unique<SparseContainerCheckpointer>(heartbeat_, capacity);
                ++num_sparses_;
            } else
            {
                checkpointers_[cid] = std::make_unique<ContigContainerCheckpointer>(heartbeat_, capacity);
                ++num_contigs_;
            }
        }

        size_t getNumScalars() const { return num_scalars_; }

        size_t getNumContigs() const { return num_contigs_; }

        size_t getNumSparses() const { return num_sparses_; }

        /// Must be called from main thread
        void writeMetaOnPostTeardown(DatabaseManager* db_mgr)
        {
            writeShowInUI_(db_mgr);
            writeQueueMaxSizes_(db_mgr);
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            auto action = pipeline::PipelineAction::SLEEP;

            LedgerPtr ledger;
            while (main_input_queue_->try_pop(ledger))
            {
                auto window_id = ledger->getWindowId();

                auto scalars = ledger->releaseScalarEntries();
                for (auto& scalar : scalars)
                {
                    auto cid = scalar.cid;
                    auto data = std::move(scalar.scalar_bytes);
                    checkpointers_.at(cid)->createCheckpoint(window_id, std::move(data));
                }

                auto contigs = ledger->releaseContigEntries();
                for (auto& contig : contigs)
                {
                    auto cid = contig.cid;
                    auto data = std::move(contig.contig_bin_bytes);
                    checkpointers_.at(cid)->createCheckpoint(window_id, std::move(data));
                    updateContainerMaxSize_(cid, detail::getContainerSize(data));
                }

                auto sparses = ledger->releaseSparseEntries();
                for (auto& sparse : sparses)
                {
                    auto cid = sparse.cid;
                    auto data = std::move(sparse.sparse_bin_bytes);
                    checkpointers_.at(cid)->createCheckpoint(window_id, std::move(data));
                    updateContainerMaxSize_(cid, detail::getContainerSize(data));
                }

                for (const auto cid : ledger->getClosedCIDs())
                {
                    checkpointers_.at(cid)->closeRecord(window_id);
                }

                CollectionEntries to_send;
                auto sim_time = ledger->getSimTime();
                to_send.sim_time = sim_time;

                const auto multi_clock = clock_ids_.size() > 1;
                for (auto& [cid, checkpointer] : checkpointers_)
                {
                    if (multi_clock && !checkpointer->participatedInWindow(window_id))
                    {
                        continue;
                    }

                    auto entries = checkpointer->encodeForPipeline(window_id, sim_time, cid);
                    for (auto& entry : entries)
                    {
                        cids_with_data_.insert(cid);
                        to_send.entries.emplace_back(std::move(entry));
                        if (const auto clk_it = collectable_clock_ids_.find(cid);
                            clk_it != collectable_clock_ids_.end())
                        {
                            to_send.clock_ids.insert(clk_it->second);
                        }
                    }
                }

                if (!to_send.entries.empty())
                {
                    data_output_queue_->emplace(std::move(to_send));
                }

                action = pipeline::PipelineAction::PROCEED;
            }

            return action;
        }

        void updateContainerMaxSize_(uint16_t cid, uint16_t size)
        {
            auto it = container_max_sizes_.find(cid);
            if (it == container_max_sizes_.end())
            {
                container_max_sizes_.emplace(cid, size);
            } else
            {
                it->second = std::max(it->second, size);
            }
        }

        // Set ShowInUI=1 for all the collectables that actually collected data
        void writeShowInUI_(DatabaseManager* db_mgr) const
        {
            const std::vector<int> valid_cids(cids_with_data_.begin(), cids_with_data_.end());
            if (!valid_cids.empty())
            {
                std::ostringstream oss;
                oss << "UPDATE CollectableTreeNodes SET ShowInUI=1 WHERE CID IN (";

                bool comma = false;
                for (const auto cid : valid_cids)
                {
                    if (comma)
                    {
                        oss << ",";
                    }
                    oss << cid;
                    comma = true;
                }
                oss << ")";
                db_mgr->EXECUTE(oss.str());
            }

            auto query = db_mgr->createQuery("CollectableTreeNodes");
            query->addConstraintForInt("CID", SetConstraints::NOT_IN_SET, valid_cids);

            struct CID_Info
            {
                std::string path;
                std::string type;

                CID_Info(const std::string& path, const std::string& type) :
                    path(path),
                    type(type)
                {
                }
            };

            std::string path;
            query->select("FullPath", path);

            std::string type;
            query->select("TypeName", type);

            std::vector<CID_Info> cid_infos;
            auto results = query->getResultSet();
            while (results.getNextRecord())
            {
                cid_infos.emplace_back(path, type);
            }

            if (!cid_infos.empty())
            {
                std::ostringstream oss;
                oss << "No data was ever collected for the following collectables, and will not be shown in Argos:\n";
                size_t leftcol_w = 0;
                for (const auto& info : cid_infos)
                {
                    leftcol_w = std::max(leftcol_w, info.path.size());
                }

                leftcol_w += 12;
                for (const auto& info : cid_infos)
                {
                    oss << std::left << std::setw(leftcol_w) << info.path;
                    if (auto idx = info.type.find("_sparse"); idx != std::string::npos)
                    {
                        auto base_type = info.type.substr(0, idx);
                        oss << "(Sparse container of '" << base_type << "')";
                    } else if (auto idx = info.type.find("_contig"); idx != std::string::npos)
                    {
                        auto base_type = info.type.substr(0, idx);
                        oss << "(Contig container of '" << base_type << "')";
                    } else
                    {
                        oss << "(" << info.type << ")";
                    }
                    oss << "\n";
                }

                auto warning = oss.str();
                warning.pop_back();

                db_mgr->INSERT(SQL_TABLE("Notifications"), SQL_COLUMNS("NotifStr", "NotifType"),
                               SQL_VALUES(warning, (int)NotifType::WARNING));
            }
        }

        void writeQueueMaxSizes_(DatabaseManager* db_mgr) const
        {
            auto inserter = db_mgr->prepareINSERT(SQL_TABLE("QueueMaxSizes"));
            for (const auto& [cid, max_size] : container_max_sizes_)
            {
                inserter->createRecordWithColValues((int)cid, (int)max_size);
            }
        }

        const size_t heartbeat_;

        ConcurrentQueue<LedgerPtr>* main_input_queue_ = nullptr;
        ConcurrentQueue<CollectionEntries>* data_output_queue_ = nullptr;

        std::unordered_map<uint16_t, uint32_t> collectable_clock_ids_;
        std::unordered_set<uint32_t> clock_ids_;
        std::unordered_set<uint16_t> cids_with_data_;

        std::unordered_map<uint16_t, std::unique_ptr<CollectableCheckpointer>> checkpointers_;
        std::unordered_map<uint16_t, uint16_t> container_max_sizes_;

        size_t num_scalars_ = 0;
        size_t num_contigs_ = 0;
        size_t num_sparses_ = 0;
    };

    /// \class Compressor
    /// \brief 2nd async stage. Performs zlib compression.
    class Compressor : public pipeline::CompressionStage
    {
    public:
        Compressor()
        {
            addInPort_<CollectionEntries>("data_input_queue", input_queue_);
            addOutPort_<CompressedCollectionEntries>("data_output_queue", output_queue_);
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            CollectionEntries collection_at_time;
            if (input_queue_->try_pop(collection_at_time))
            {
                std::vector<char> uncompressed;
                for (const auto& src : collection_at_time.entries)
                {
                    const auto& src_data = src->getData();
                    uncompressed.insert(uncompressed.end(), src_data.begin(), src_data.end());
                }

                CompressedCollectionEntries compressed;
                compressData(uncompressed, compressed.compressed_entries, getBestCompressionLevel_());
                compressed.sim_time = collection_at_time.sim_time;
                compressed.clock_ids = std::move(collection_at_time.clock_ids);
                output_queue_->emplace(std::move(compressed));
                return pipeline::PipelineAction::PROCEED;
            }

            return pipeline::PipelineAction::SLEEP;
        }

        ConcurrentQueue<CollectionEntries>* input_queue_ = nullptr;
        ConcurrentQueue<CompressedCollectionEntries>* output_queue_ = nullptr;
    };

    /// \class Writer
    /// \brief Final async stage. Writes collected/checkpointed/compressed data and notifications
    /// to the database.
    class Writer : public pipeline::DatabaseStage<ArgosCollector>
    {
    public:
        Writer()
        {
            addInPort_<CompressedCollectionEntries>("data_input_queue", data_input_queue_);
            addInPort_<NotifEntry>("notif_input_queue", notif_input_queue_);
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            auto action = pipeline::PipelineAction::SLEEP;

            CompressedCollectionEntries collection_at_time;
            if (data_input_queue_->try_pop(collection_at_time))
            {
                auto db_mgr = getDatabaseManager_();
                auto id = Timestamp::createTimestampInDatabase(db_mgr, collection_at_time.sim_time);

                auto inserter = getTableInserter_("CollectionRecords");
                const auto& bytes = collection_at_time.compressed_entries;
                inserter->createRecordWithColValues(id, bytes);

                if (!collection_at_time.clock_ids.empty())
                {
                    auto clk_inserter = getTableInserter_("TimestampClocks");
                    for (const auto clock_id : collection_at_time.clock_ids)
                    {
                        clk_inserter->createRecordWithColValues(id, static_cast<int>(clock_id));
                    }
                }

                action = pipeline::PipelineAction::PROCEED;
            }

            // Notifications are expected to be "sporadic" / one-off. Flush all
            // of the notifs now, and do not consider "notification-only" to mean
            // "keep greedily processing / keep threads running". This is the
            // reason why we use WHILE here instead of IF like above, and the
            // returned action is not set to PROCEED based on the availability
            // of new notifications.
            NotifEntry notif;
            while (notif_input_queue_->try_pop(notif))
            {
                auto inserter = getTableInserter_("Notifications");

                const auto& notif_str = notif.notif;
                const auto notif_type = notif.type;

                if (notif.sim_time.isValid())
                {
                    inserter->setColumnValue(0, notif.sim_time.getValue());
                }
                inserter->setColumnValue(1, (int)notif_type);
                inserter->setColumnValue(2, notif_str);
                inserter->createRecord();
            }

            return action;
        }

        ConcurrentQueue<CompressedCollectionEntries>* data_input_queue_ = nullptr;
        ConcurrentQueue<NotifEntry>* notif_input_queue_ = nullptr;
    };

    void assertLive_() const
    {
        simdb_assert(is_live_ && timestamp_, "API call cannot be made until pipeline is open and "
                                             "timestampWith() was called");
    }

    void checkTimeAdvanced_()
    {
        assertLive_();

        auto current_time = timestamp_->getTime();
        if (!current_stage_time_.isValid())
        {
            current_stage_time_ = current_time;
            ledger_ = std::make_unique<Ledger>(current_time, current_window_id_++, pipeline_stager_->getNumScalars(),
                                               pipeline_stager_->getNumContigs(), pipeline_stager_->getNumSparses());
        } else if (current_time < current_stage_time_.getValue())
        {
            simdb_assert(current_time >= current_stage_time_.getValue(), "Time must be monotonically increasing");
        } else if (current_time > current_stage_time_.getValue())
        {
            if (ledger_)
            {
                // Send current ledger to the pipeline and create a new one.
                pipeline_head_->emplace(std::move(ledger_));

                ledger_ =
                    std::make_unique<Ledger>(current_time, current_window_id_++, pipeline_stager_->getNumScalars(),
                                             pipeline_stager_->getNumContigs(), pipeline_stager_->getNumSparses());
            }

            current_stage_time_ = current_time;
        }
    }

    struct ContainerMeta
    {
        ValidValue<bool> sparse;
        ValidValue<size_t> capacity;
    };

    static bool extractContainerMeta_(const std::string& encoded_dtype, ContainerMeta& meta)
    {
        auto extract_capacity = [&](const bool contig) {
            const std::string lookfor = contig ? "_contig_capacity" : "_sparse_capacity";
            if (auto idx = encoded_dtype.find(lookfor); idx != std::string::npos)
            {
                auto capacity = std::stoi(encoded_dtype.substr(idx + lookfor.size()));
                meta.sparse = !contig;
                meta.capacity = capacity;
                return true;
            }
            return false;
        };

        return extract_capacity(true) || extract_capacity(false);
    }

    uint32_t getClockId_(const std::string& clk_name) const
    {
        for (size_t i = 0; i < clocks_.size(); ++i)
        {
            if (std::get<0>(clocks_[i]) == clk_name)
            {
                return static_cast<uint32_t>(i + 1);
            }
        }
        throw DBException("Unknown clock: ") << clk_name;
    }

    DatabaseManager* const db_mgr_;
    size_t heartbeat_ = DEFAULT_HEARTBEAT;

    using ClockDescriptor = std::tuple<std::string, // clk name
                                       uint32_t,    // period
                                       uint32_t,    // numerator
                                       uint32_t     // denominator
                                       >;
    std::vector<ClockDescriptor> clocks_;

    using CollectableMeta = std::tuple<std::string, // dot-delimited path
                                       std::string, // clk name
                                       std::string  // encoded data type
                                       >;
    std::map<uint16_t, CollectableMeta> meta_by_cid_;

    std::unique_ptr<Timestamp> timestamp_;
    std::vector<std::unique_ptr<EntryPoint>> entry_points_;
    PipelineStager* pipeline_stager_ = nullptr;
    ValidValue<uint64_t> current_stage_time_;
    uint64_t current_window_id_ = 1;
    TinyStrings<> tiny_strings_;
    EnumInspector enum_inspector_;
    bool is_live_ = false;
};

} // namespace simdb::argos
