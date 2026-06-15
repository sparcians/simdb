#include "SimDBTester.hpp"
#include "simdb/apps/AppManager.hpp"
#include "simdb/apps/argos/ArgosCollector.hpp"
#include "simdb/utils/Compress.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <vector>

TEST_INIT;

namespace {

constexpr size_t kHeartbeat = 3;
constexpr size_t kContainerCapacity = 8;
uint64_t kRunTicks = 100'000;
constexpr uint64_t kSeed = 0xA5A5A5A5ULL;

std::filesystem::path workDir()
{
    auto dir = std::filesystem::current_path() / "test" / "argos" / "async_encoding_runs";
    std::filesystem::create_directories(dir);
    return dir;
}

std::vector<char> scalarBytes(uint8_t val)
{
    return std::vector<char>{static_cast<char>(val)};
}

std::vector<std::vector<char>> contigBytes(uint8_t a, uint8_t b, uint8_t c)
{
    return {{static_cast<char>(a)}, {static_cast<char>(b)}, {static_cast<char>(c)}};
}

std::map<uint16_t, std::vector<char>> sparseBytes(uint16_t idx, uint8_t val)
{
    return {{idx, scalarBytes(val)}};
}

std::filesystem::path runSimulation(const bool async_encoding)
{
    simdb::argos::CollectionEntryPoint::resetCIDs();

    const auto db_name = async_encoding ? "async_encoding.db" : "sync_encoding.db";
    const auto db_path = workDir() / db_name;
    std::filesystem::remove(db_path);

    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::argos::ArgosCollector>();
    auto& app_mgr = app_mgrs.createAppManager(db_path.string());

    app_mgr.enableApp(simdb::argos::ArgosCollector::NAME);
    app_mgrs.createEnabledApps();
    auto argos_collector = app_mgr.getApp<simdb::argos::ArgosCollector>();
    argos_collector->setHeartbeat(kHeartbeat);
    argos_collector->enableAsyncEncoding(async_encoding);

    auto fast_scalar = argos_collector->createScalarCollector("top.fast_scalar", "fast", "unsigned char");
    auto slow_scalar = argos_collector->createScalarCollector("top.slow_scalar", "slow", "unsigned char");
    auto contig = argos_collector->createContainerCollector("top.contig", "fast", "unsigned char_contig_capacity8");
    auto sparse = argos_collector->createContainerCollector("top.sparse", "slow", "unsigned char_sparse_capacity8");

    uint64_t tick = 0;
    argos_collector->timestampWith(&tick);
    argos_collector->addClock("fast", 1);
    argos_collector->addClock("slow", 4);

    app_mgrs.createSchemas();
    app_mgrs.postInit(0, nullptr);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    std::mt19937_64 rng(kSeed);
    std::uniform_int_distribution<int> roll(0, 99);

    while (++tick <= kRunTicks)
    {
        const int r = roll(rng);

        if (r < 70)
        {
            fast_scalar->setScalarValueBytes(scalarBytes(static_cast<uint8_t>(tick & 0xFF)));
        }

        if (r < 25)
        {
            slow_scalar->setScalarValueBytes(scalarBytes(static_cast<uint8_t>((tick * 3) & 0xFF)));
        }

        if (r < 60)
        {
            contig->setContigContainerBinBytes(contigBytes(static_cast<uint8_t>(tick),
                                                          static_cast<uint8_t>(tick + 1),
                                                          static_cast<uint8_t>(tick + 2)));
        }

        if (r < 20)
        {
            sparse->setSparseContainerBinBytes(sparseBytes(static_cast<uint16_t>(tick % kContainerCapacity),
                                                          static_cast<uint8_t>(tick & 0xFF)));
        }
    }

    app_mgrs.postSimLoopTeardown();

    return db_path;
}

using RecordsByTime = std::map<uint64_t, std::vector<char>>;

RecordsByTime loadCompressedRecords(simdb::DatabaseManager* db_mgr)
{
    RecordsByTime out;

    auto query = db_mgr->createQuery("Timestamps");
    uint64_t sim_time = 0;
    query->select("Timestamp", sim_time);

    auto results = query->getResultSet();
    while (results.getNextRecord())
    {
        auto ts_query = db_mgr->createQuery("Timestamps");
        ts_query->addConstraintForUInt64("Timestamp", simdb::Constraints::EQUAL, sim_time);
        int timestamp_id = 0;
        ts_query->select("Id", timestamp_id);
        auto ts_results = ts_query->getResultSet();
        if (!ts_results.getNextRecord())
        {
            continue;
        }

        auto rec_query = db_mgr->createQuery("CollectionRecords");
        rec_query->addConstraintForInt32("TimestampID", simdb::Constraints::EQUAL, timestamp_id);
        std::vector<char> blob;
        rec_query->select("Records", blob);
        auto rec_results = rec_query->getResultSet();
        if (rec_results.getNextRecord())
        {
            out.emplace(sim_time, std::move(blob));
        }
    }

    return out;
}

RecordsByTime decompressRecords(const RecordsByTime& compressed)
{
    RecordsByTime out;
    for (const auto& [sim_time, blob] : compressed)
    {
        std::vector<char> uncompressed;
        simdb::decompressData(blob, uncompressed);
        out.emplace(sim_time, std::move(uncompressed));
    }
    return out;
}

void testSyncAndAsyncProduceIdenticalCollectionRecords()
{
    const auto sync_db = runSimulation(false);
    const auto async_db = runSimulation(true);

    simdb::DatabaseManager sync_mgr(sync_db.string());
    simdb::DatabaseManager async_mgr(async_db.string());

    const auto sync_records = decompressRecords(loadCompressedRecords(&sync_mgr));
    const auto async_records = decompressRecords(loadCompressedRecords(&async_mgr));

    EXPECT_EQUAL(sync_records.size(), async_records.size());

    for (const auto& [sim_time, sync_bytes] : sync_records)
    {
        const auto async_it = async_records.find(sim_time);
        EXPECT_TRUE(async_it != async_records.end());
        if (async_it == async_records.end())
        {
            continue;
        }
        EXPECT_EQUAL(sync_bytes, async_it->second);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        kRunTicks = std::strtoull(argv[1], nullptr, 10);
    }
    testSyncAndAsyncProduceIdenticalCollectionRecords();

    REPORT_ERROR;
    return ERROR_CODE;
}
