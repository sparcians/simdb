// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/schema/SchemaDef.hpp"
#include "simdb/utils/Compress.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using simdb::argos::PipelineStager;
using simdb::argos::QueueCollectionData;
using simdb::argos::Timestamp;

constexpr size_t kContainerCapacity = 16;
constexpr char kContigDtype[] = "unsigned char_contig_capacity16";
constexpr char kSparseDtype[] = "unsigned char_sparse_capacity16";
constexpr char kScalarDtype[] = "unsigned char";

constexpr uint16_t kFastScalarCid = 10;
constexpr uint16_t kSlowScalarCid = 20;
constexpr uint16_t kFastContigCid = 27;
constexpr uint16_t kSlowContigCid = 37;
constexpr uint16_t kFastSparseCid = 47;
constexpr uint16_t kSlowSparseCid = 57;

constexpr uint64_t kDefaultSeed = 0xC0FFEEULL;
constexpr const char* kCondaEnv = "sparta";

std::filesystem::path findRepoRoot()
{
    auto path = std::filesystem::current_path();
    for (int depth = 0; depth < 12; ++depth)
    {
        if (std::filesystem::exists(path / "python" / "argos" / "tester.py"))
        {
            return path;
        }
        if (!path.has_parent_path() || path == path.parent_path())
        {
            break;
        }
        path = path.parent_path();
    }
    throw simdb::DBException("Could not locate repo root (python/argos/tester.py)");
}

std::filesystem::path integrationWorkDir()
{
    auto dir = std::filesystem::current_path() / "test" / "argos" / "integration_runs";
    std::filesystem::create_directories(dir);
    return dir;
}

void writeCollectionRecord(simdb::DatabaseManager* db_mgr, const QueueCollectionData& entry)
{
    std::vector<char> uncompressed;
    for (const auto& src : entry.entries)
    {
        const auto& src_data = src->getData();
        uncompressed.insert(uncompressed.end(), src_data.begin(), src_data.end());
    }

    std::vector<char> compressed;
    simdb::compressData(uncompressed, compressed);

    const auto timestamp_id = Timestamp::createTimestampInDatabase(db_mgr, entry.sim_time.getValue());
    db_mgr->INSERT(SQL_TABLE("CollectionRecords"), SQL_VALUES(timestamp_id, compressed));

    for (const auto clock_id : entry.clock_ids)
    {
        db_mgr->INSERT(SQL_TABLE("TimestampClocks"), SQL_VALUES(timestamp_id, static_cast<int>(clock_id)));
    }
}

void drainPipelineQueue(simdb::ConcurrentQueue<QueueCollectionData>* queue, simdb::DatabaseManager* db_mgr)
{
    QueueCollectionData entry;
    while (queue->try_pop(entry))
    {
        writeCollectionRecord(db_mgr, entry);
    }
}

void markAllCollectablesVisible(simdb::DatabaseManager* db_mgr)
{
    db_mgr->EXECUTE("UPDATE CollectableTreeNodes SET ShowInUI=1");
}

struct CollectableSpec
{
    uint16_t cid;
    const char* path;
    const char* dtype;
    bool sparse;
    bool is_scalar;
    double stage_probability;
};

const std::vector<CollectableSpec>& collectableSpecs()
{
    static const std::vector<CollectableSpec> specs = {
        {kFastScalarCid, "top.fast_scalar", kScalarDtype, false, true, 0.70},
        {kSlowScalarCid, "top.slow_scalar", kScalarDtype, false, true, 0.25},
        {kFastContigCid, "top.fast_contig", kContigDtype, false, false, 0.70},
        {kSlowContigCid, "top.slow_contig", kContigDtype, false, false, 0.25},
        {kFastSparseCid, "top.fast_sparse", kSparseDtype, true, false, 0.70},
        {kSlowSparseCid, "top.slow_sparse", kSparseDtype, true, false, 0.25},
    };
    return specs;
}

void bootstrapDatabaseSchema(simdb::DatabaseManager* db_mgr)
{
    using dt = simdb::SqlDataType;
    simdb::Schema schema;

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
    collectable_tns_tbl.addColumn("SerializationCID", dt::int32_t);
    collectable_tns_tbl.addColumn("FullPath", dt::string_t);
    collectable_tns_tbl.addColumn("ClockID", dt::int32_t);
    collectable_tns_tbl.addColumn("TypeName", dt::string_t);
    collectable_tns_tbl.addColumn("ShowInUI", dt::int32_t);
    collectable_tns_tbl.setColumnDefaultValue("ShowInUI", 0);
    collectable_tns_tbl.ensureUnique("SerializationCID");
    collectable_tns_tbl.createIndexOn("SerializationCID");
    collectable_tns_tbl.unsetPrimaryKey();

    auto& dtype_schemas_tbl = schema.addTable("DataTypeSchemas");
    dtype_schemas_tbl.addColumn("RootTypeName", dt::string_t);

    auto& dtype_nodes_tbl = schema.addTable("DataTypeNodes");
    dtype_nodes_tbl.addColumn("SchemaId", dt::int32_t);
    dtype_nodes_tbl.addColumn("Name", dt::string_t);
    dtype_nodes_tbl.addColumn("TypeName", dt::string_t);
    dtype_nodes_tbl.addColumn("FormatStr", dt::string_t);

    auto& signed_enum_map_tbl = schema.addTable("SignedEnumMappings");
    signed_enum_map_tbl.addColumn("EnumName", dt::string_t);
    signed_enum_map_tbl.addColumn("EnumString", dt::string_t);
    signed_enum_map_tbl.addColumn("EnumInt", dt::int64_t);

    auto& unsigned_enum_map_tbl = schema.addTable("UnsignedEnumMappings");
    unsigned_enum_map_tbl.addColumn("EnumName", dt::string_t);
    unsigned_enum_map_tbl.addColumn("EnumString", dt::string_t);
    unsigned_enum_map_tbl.addColumn("EnumInt", dt::uint64_t);

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
    queue_max_sizes_tbl.addColumn("SerializationCID", dt::int32_t);
    queue_max_sizes_tbl.addColumn("MaxSize", dt::int32_t);
    queue_max_sizes_tbl.ensureUnique("SerializationCID");
    queue_max_sizes_tbl.unsetPrimaryKey();

    auto& notif_tbl = schema.addTable("Notifications");
    notif_tbl.addColumn("SerializationCID", dt::int32_t);
    notif_tbl.addColumn("Timestamp", dt::uint64_t);
    notif_tbl.addColumn("NotifType", dt::int32_t);
    notif_tbl.addColumn("NotifStr", dt::string_t);

    auto& dyn_field_type_changes_tbl = schema.addTable("DynamicFieldTypeChanges");
    dyn_field_type_changes_tbl.addColumn("SerializationCID", dt::int32_t);
    dyn_field_type_changes_tbl.addColumn("FieldTypes", dt::string_t);
    dyn_field_type_changes_tbl.addColumn("Timestamp", dt::uint64_t);
    dyn_field_type_changes_tbl.createCompoundIndexOn({"SerializationCID", "Timestamp"});

    auto& dyn_field_names_tbl = schema.addTable("DynamicFieldNames");
    dyn_field_names_tbl.addColumn("SerializationCID", dt::int32_t);
    dyn_field_names_tbl.addColumn("FieldNames", dt::string_t);
    dyn_field_names_tbl.createIndexOn("SerializationCID");

    auto& tiny_string_ids_tbl = schema.addTable("TinyStringIDs");
    tiny_string_ids_tbl.addColumn("StringValue", dt::string_t);
    tiny_string_ids_tbl.addColumn("StringID", dt::uint32_t);

    db_mgr->appendSchema(schema);
}

void bootstrapMetadata(simdb::DatabaseManager* db_mgr, size_t heartbeat)
{
    db_mgr->INSERT(SQL_TABLE("CollectionGlobals"), SQL_VALUES(static_cast<int>(heartbeat)));

    const auto clk_id = db_mgr->INSERT(SQL_TABLE("Clocks"), SQL_VALUES("root", 1, 0, 0))->getId();

    auto ctn_inserter = db_mgr->prepareINSERT(SQL_TABLE("CollectableTreeNodes"));
    for (const auto& spec : collectableSpecs())
    {
        ctn_inserter->createRecordWithColValues(static_cast<int>(spec.cid), spec.path, clk_id, spec.dtype, 0);
    }
}

void bootstrapMetadataDualClock(simdb::DatabaseManager* db_mgr, size_t heartbeat)
{
    db_mgr->INSERT(SQL_TABLE("CollectionGlobals"), SQL_VALUES(static_cast<int>(heartbeat)));

    const auto fast_clk_id = db_mgr->INSERT(SQL_TABLE("Clocks"), SQL_VALUES("fast", 1, 0, 0))->getId();
    const auto slow_clk_id = db_mgr->INSERT(SQL_TABLE("Clocks"), SQL_VALUES("slow", 5, 0, 0))->getId();

    auto ctn_inserter = db_mgr->prepareINSERT(SQL_TABLE("CollectableTreeNodes"));
    for (const auto& spec : collectableSpecs())
    {
        const auto is_fast = std::string(spec.path).find(".fast_") != std::string::npos;
        const auto clk_id = is_fast ? fast_clk_id : slow_clk_id;
        ctn_inserter->createRecordWithColValues(static_cast<int>(spec.cid), spec.path, clk_id, spec.dtype, 0);
    }
}

bool isFastCollectablePath(const char* path)
{
    return std::string(path).find(".fast_") != std::string::npos;
}

std::vector<char> scalarByte(char tag)
{
    return {tag};
}

std::vector<std::vector<char>> contigFromLiteral(const char* literal)
{
    std::vector<std::vector<char>> bins;
    for (const char* cursor = literal; *cursor != '\0'; ++cursor)
    {
        bins.push_back({*cursor});
    }
    return bins;
}

std::map<uint16_t, std::vector<char>> sparseFromLiteral(const char* literal, uint16_t base_index = 1)
{
    std::map<uint16_t, std::vector<char>> bins;
    uint16_t idx = base_index;
    for (const char* cursor = literal; *cursor != '\0'; ++cursor)
    {
        bins.emplace(idx, std::vector<char>{*cursor});
        idx += 2;
    }
    return bins;
}

enum class ScenarioEventKind {
    AdvanceTime,
    StageScalar,
    StageContig,
    StageSparse,
    Disable,
    Enable,
};

struct ScenarioEvent
{
    uint64_t sim_time = 0;
    ScenarioEventKind kind = ScenarioEventKind::AdvanceTime;
    uint16_t cid = 0;
    std::vector<char> scalar_payload;
    std::vector<std::vector<char>> contig_payload;
    std::map<uint16_t, std::vector<char>> sparse_payload;
};

ScenarioEvent makeAdvance(uint64_t sim_time)
{
    return {sim_time, ScenarioEventKind::AdvanceTime, 0, {}, {}, {}};
}

ScenarioEvent makeStageScalar(uint64_t sim_time, uint16_t cid, std::vector<char> payload)
{
    return {sim_time, ScenarioEventKind::StageScalar, cid, std::move(payload), {}, {}};
}

ScenarioEvent makeStageContig(uint64_t sim_time, uint16_t cid, std::vector<std::vector<char>> payload)
{
    return {sim_time, ScenarioEventKind::StageContig, cid, {}, std::move(payload), {}};
}

ScenarioEvent makeStageSparse(uint64_t sim_time, uint16_t cid, std::map<uint16_t, std::vector<char>> payload)
{
    return {sim_time, ScenarioEventKind::StageSparse, cid, {}, {}, std::move(payload)};
}

ScenarioEvent makeDisable(uint64_t sim_time, uint16_t cid)
{
    return {sim_time, ScenarioEventKind::Disable, cid, {}, {}, {}};
}

ScenarioEvent makeEnable(uint64_t sim_time, uint16_t cid)
{
    return {sim_time, ScenarioEventKind::Enable, cid, {}, {}, {}};
}

class ScenarioScript
{
public:
    explicit ScenarioScript(uint64_t seed) : rng_(seed) {}

    static ScenarioScript generateSmoke()
    {
        ScenarioScript script(kDefaultSeed);
        script.events_ = {
            makeStageScalar(100, kFastScalarCid, scalarByte('F')),
            makeStageScalar(100, kSlowScalarCid, scalarByte('S')),
            makeStageContig(100, kFastContigCid, contigFromLiteral("ABC")),
            makeStageScalar(101, kFastScalarCid, scalarByte('F')),
            makeStageContig(101, kFastContigCid, contigFromLiteral("ABC")),
            makeStageScalar(102, kFastScalarCid, scalarByte('F')),
            makeStageContig(102, kFastContigCid, contigFromLiteral("AXC")),
        };
        return script;
    }

    static ScenarioScript generateDualClock()
    {
        ScenarioScript script(kDefaultSeed);
        for (uint64_t sim_time = 100; sim_time <= 124; ++sim_time)
        {
            script.events_.push_back(makeStageScalar(sim_time, kFastScalarCid, scalarByte('F')));
            if (sim_time % 5 == 0)
            {
                script.events_.push_back(makeStageScalar(sim_time, kSlowScalarCid, scalarByte('S')));
            }
        }
        return script;
    }

    static ScenarioScript generateRandom(uint64_t seed, size_t num_steps = 400)
    {
        ScenarioScript script(seed);
        std::uniform_int_distribution<int> delta_dist(0, 5);
        static const int kDeltas[] = {1, 1, 1, 2, 3, 5, 10, 20};
        std::uniform_int_distribution<size_t> action_dist(0, 99);

        uint64_t sim_time = 100;
        char scalar_tag = 'A';
        char contig_tag = 'a';
        char sparse_tag = 'q';

        struct LiveState
        {
            bool enabled = true;
            bool has_data = false;
            std::vector<char> scalar_payload;
            std::vector<std::vector<char>> contig_payload;
            std::map<uint16_t, std::vector<char>> sparse_payload;
        };

        std::map<uint16_t, LiveState> state;
        for (const auto& spec : collectableSpecs())
        {
            (void)spec;
            state[spec.cid] = LiveState{};
        }

        for (size_t step = 0; step < num_steps; ++step)
        {
            sim_time += static_cast<uint64_t>(kDeltas[delta_dist(script.rng_)]);
            script.events_.push_back(makeAdvance(sim_time));

            for (const auto& spec : collectableSpecs())
            {
                auto& live = state[spec.cid];
                const auto roll = action_dist(script.rng_);

                if (!live.enabled)
                {
                    if (roll < 8 && live.has_data)
                    {
                        script.events_.push_back(makeEnable(sim_time, spec.cid));
                        live.enabled = true;
                    }
                    continue;
                }

                if (roll < 6 && live.has_data)
                {
                    script.events_.push_back(makeDisable(sim_time, spec.cid));
                    live.enabled = false;
                    continue;
                }

                if (static_cast<unsigned>(roll) > static_cast<unsigned>(spec.stage_probability * 100.0))
                {
                    continue;
                }

                if (spec.is_scalar)
                {
                    live.scalar_payload = scalarByte(scalar_tag++);
                    script.events_.push_back(makeStageScalar(sim_time, spec.cid, live.scalar_payload));
                    live.has_data = true;
                } else if (spec.sparse)
                {
                    if (!live.has_data || roll < 40)
                    {
                        live.sparse_payload = sparseFromLiteral("abc");
                    } else if (roll < 70)
                    {
                        for (auto& [idx, bytes] : live.sparse_payload)
                        {
                            if (!bytes.empty())
                            {
                                bytes[0] = sparse_tag++;
                            }
                        }
                    } else
                    {
                        if (!live.sparse_payload.empty())
                        {
                            auto it = live.sparse_payload.begin();
                            if (std::next(it) != live.sparse_payload.end())
                            {
                                live.sparse_payload.erase(std::next(it));
                            }
                        }
                    }
                    script.events_.push_back(makeStageSparse(sim_time, spec.cid, live.sparse_payload));
                    live.has_data = !live.sparse_payload.empty();
                } else
                {
                    if (!live.has_data || roll < 40)
                    {
                        live.contig_payload = contigFromLiteral("abc");
                    } else if (roll < 75)
                    {
                        if (!live.contig_payload.empty() && live.contig_payload.size() > 1)
                        {
                            live.contig_payload[1] = {contig_tag++};
                        }
                    } else if (live.contig_payload.size() > 1)
                    {
                        live.contig_payload.erase(live.contig_payload.begin() + 1);
                    } else
                    {
                        live.contig_payload = contigFromLiteral("xyz");
                    }
                    script.events_.push_back(makeStageContig(sim_time, spec.cid, live.contig_payload));
                    live.has_data = !live.contig_payload.empty();
                }
            }
        }

        return script;
    }

    const std::vector<ScenarioEvent>& events() const { return events_; }

private:
    std::mt19937_64 rng_;
    std::vector<ScenarioEvent> events_;
};

class IntegrationRun
{
public:
    explicit IntegrationRun(std::filesystem::path db_path) :
        db_path_(std::move(db_path)),
        db_mgr_(db_path_.string(), true)
    {
        bootstrapDatabaseSchema(&db_mgr_);
    }

    void configure(size_t heartbeat, bool dual_clock = false)
    {
        heartbeat_ = heartbeat;
        if (dual_clock)
        {
            bootstrapMetadataDualClock(&db_mgr_, heartbeat_);
        } else
        {
            bootstrapMetadata(&db_mgr_, heartbeat_);
        }

        timestamp_ = std::make_unique<Timestamp>(&sim_time_);
        stager_ = std::make_unique<PipelineStager>(heartbeat_, timestamp_.get(), &pipeline_queue_);
        for (const auto& spec : collectableSpecs())
        {
            if (spec.is_scalar)
            {
                stager_->setScalarType(spec.cid);
            } else
            {
                stager_->setContainerType(spec.cid, spec.sparse, kContainerCapacity);
            }

            if (dual_clock)
            {
                stager_->setCollectableClock(spec.cid, isFastCollectablePath(spec.path) ? 1u : 2u);
            } else
            {
                stager_->setCollectableClock(spec.cid, 1u);
            }
        }
    }

    void replay(const ScenarioScript& script)
    {
        uint64_t last_time = 0;
        for (const auto& event : script.events())
        {
            if (event.sim_time < last_time)
            {
                throw simdb::DBException("Scenario time must be monotonic");
            }

            if (event.kind == ScenarioEventKind::AdvanceTime)
            {
                sim_time_ = event.sim_time;
                stager_->advanceSimTimeSlot();
                drainPipelineQueue(&pipeline_queue_, &db_mgr_);
                last_time = event.sim_time;
                continue;
            }

            sim_time_ = event.sim_time;
            switch (event.kind)
            {
            case ScenarioEventKind::StageScalar:
                stager_->stage(event.cid, event.scalar_payload);
                break;
            case ScenarioEventKind::StageContig:
                stager_->stage(event.cid, event.contig_payload);
                break;
            case ScenarioEventKind::StageSparse:
                stager_->stage(event.cid, event.sparse_payload);
                break;
            case ScenarioEventKind::Disable:
                stager_->onEnabledChanged(event.cid, false);
                break;
            case ScenarioEventKind::Enable:
                stager_->onEnabledChanged(event.cid, true);
                break;
            default:
                break;
            }

            stager_->advanceSimTimeSlot();
            drainPipelineQueue(&pipeline_queue_, &db_mgr_);
            last_time = event.sim_time;
        }

        stager_->sendCollectedDataToPipeline();
        drainPipelineQueue(&pipeline_queue_, &db_mgr_);
        stager_->writeMetaOnPostTeardown(&db_mgr_);
        markAllCollectablesVisible(&db_mgr_);
    }

    const std::filesystem::path& dbPath() const { return db_path_; }

private:
    std::filesystem::path db_path_;
    simdb::DatabaseManager db_mgr_;
    size_t heartbeat_ = 0;
    uint64_t sim_time_ = 0;
    std::unique_ptr<Timestamp> timestamp_;
    simdb::ConcurrentQueue<QueueCollectionData> pipeline_queue_;
    std::unique_ptr<PipelineStager> stager_;
};

class IntegrationHarness
{
public:
    explicit IntegrationHarness(uint64_t seed = kDefaultSeed) :
        repo_root_(findRepoRoot()),
        work_dir_(integrationWorkDir()),
        random_script_(ScenarioScript::generateRandom(seed)),
        smoke_script_(ScenarioScript::generateSmoke())
    {
    }

    std::filesystem::path run(size_t heartbeat, const ScenarioScript& script, const std::string& tag,
                              bool dual_clock = false)
    {
        const auto db_path =
            work_dir_ / ("integration_hb" + std::to_string(heartbeat) + "_" + tag + ".db");
        std::filesystem::remove(db_path);

        IntegrationRun run(db_path);
        run.configure(heartbeat, dual_clock);
        run.replay(script);
        return run.dbPath();
    }

    bool testerAvailable() const
    {
        return std::system(condaArgosCmd("python3 -c \"from viewer.model.data_retriever import DataRetriever\"").c_str()) ==
               0;
    }

    void compareRuns(size_t hb_a, size_t hb_b, const ScenarioScript& script)
    {
        if (!testerAvailable())
        {
            throw simdb::DBException(
                std::string("conda env '") + kCondaEnv +
                "' is unavailable or missing python/argos dependencies; run: conda activate " + kCondaEnv);
        }

        const auto db_a = run(hb_a, script, "a");
        const auto db_b = run(hb_b, script, "b");

        const auto cmd = condaArgosCmd("python3 tester.py " + db_a.string() + " " + db_b.string());

        const int rc = std::system(cmd.c_str());
        EXPECT_EQUAL(rc, 0);
        if (rc != 0)
        {
            std::ostringstream msg;
            msg << "tester.py failed (exit=" << rc << ") comparing hb " << hb_a << " vs " << hb_b
                << "\nCommand: " << cmd;
            throw simdb::DBException(msg.str());
        }
    }

    void compareRuns(size_t hb_a, size_t hb_b) { compareRuns(hb_a, hb_b, random_script_); }

    void compareDualClockRuns(size_t hb_a, size_t hb_b)
    {
        if (!testerAvailable())
        {
            throw simdb::DBException(
                std::string("conda env '") + kCondaEnv +
                "' is unavailable or missing python/argos dependencies; run: conda activate " + kCondaEnv);
        }

        const auto script = ScenarioScript::generateDualClock();
        const auto db_a = run(hb_a, script, "dual_a", true);
        const auto db_b = run(hb_b, script, "dual_b", true);

        const auto cmd = condaArgosCmd("python3 tester.py " + db_a.string() + " " + db_b.string());

        const int rc = std::system(cmd.c_str());
        EXPECT_EQUAL(rc, 0);
        if (rc != 0)
        {
            std::ostringstream msg;
            msg << "tester.py failed (exit=" << rc << ") comparing dual-clock hb " << hb_a << " vs " << hb_b
                << "\nCommand: " << cmd;
            throw simdb::DBException(msg.str());
        }
    }

    const ScenarioScript& smokeScript() const { return smoke_script_; }
    const ScenarioScript& randomScript() const { return random_script_; }

private:
    std::string condaArgosCmd(const std::string& inner_cmd) const
    {
        std::ostringstream cmd;
        cmd << "conda run -n " << kCondaEnv << " bash -lc 'cd "
            << (repo_root_ / "python" / "argos").string() << " && " << inner_cmd << "'";
        return cmd.str();
    }

    std::filesystem::path repo_root_;
    std::filesystem::path work_dir_;
    ScenarioScript random_script_;
    ScenarioScript smoke_script_;
};

void testIntegrationSmokeSameHeartbeat()
{
    IntegrationHarness harness;
    harness.compareRuns(3, 3, harness.smokeScript());
}

void testIntegrationCompare_3_10()
{
    IntegrationHarness harness;
    harness.compareRuns(3, 10);
}

void testIntegrationCompare_1_10()
{
    IntegrationHarness harness(0xABCDEFULL);
    harness.compareRuns(1, 10);
}

void testIntegrationCompare_7_100()
{
    IntegrationHarness harness(0x12345678ULL);
    harness.compareRuns(7, 100);
}

void testIntegrationDualClockCompare_3_10()
{
    IntegrationHarness harness;
    harness.compareDualClockRuns(3, 10);
}

} // namespace

TEST_INIT;

int main()
{
    testIntegrationSmokeSameHeartbeat();
    testIntegrationCompare_3_10();
    testIntegrationCompare_1_10();
    testIntegrationCompare_7_100();
    testIntegrationDualClockCompare_3_10();

    REPORT_ERROR;
    return ERROR_CODE;
}
