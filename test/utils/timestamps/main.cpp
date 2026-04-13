#include "SimDBTester.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <random>
std::random_device rd;  // Seed source for the random number engine
std::mt19937 gen(rd()); // mersenne_twister_engine

/// This test covers basic functionality of the Timestamp utility.
TEST_INIT;

struct QueryTimeFltPt
{
    using type = double;
};

struct QueryTimeNotUInt64
{
    using type = uint32_t;
};

struct QueryTimeUInt64
{
    using type = uint64_t;
};

template <typename TimeT> struct QueryTime;

template <> struct QueryTime<uint32_t> : QueryTimeNotUInt64
{
};

template <> struct QueryTime<uint64_t> : QueryTimeUInt64
{
};

template <> struct QueryTime<float> : QueryTimeFltPt
{
};

template <> struct QueryTime<double> : QueryTimeFltPt
{
};

template <typename TimeT> using query_time_t = typename QueryTime<TimeT>::type;

template <typename TimeT> void TestTimestamps()
{
    using namespace simdb::collection;

    TimeT tick = 0;
    Timestamp<TimeT> timestamp(&tick);

    simdb::DatabaseManager db_mgr("test.db", true /*new file*/);

    simdb::Schema schema;
    using dt = simdb::SqlDataType;

    auto& tbl = schema.addTable("DataBlobs");
    timestamp.addTimeColumn(tbl, "Tick");
    tbl.addColumn("DataBlob", dt::blob_t);

    db_mgr.appendSchema(schema);

    const size_t num_steps = 100;
    std::vector<size_t> base_values(1000);
    for (auto& v : base_values)
    {
        v = rand();
    }

    std::vector<std::vector<size_t>> step_values;
    for (size_t i = 0; i < num_steps; ++i)
    {
        auto values = base_values;
        std::shuffle(values.begin(), values.end(), gen);
        step_values.push_back(base_values);
    }

    db_mgr.safeTransaction([&]() {
        auto inserter = db_mgr.prepareINSERT(SQL_TABLE("DataBlobs"));
        while (++tick <= 100)
        {
            timestamp.snapshot()->apply(inserter.get());
            inserter->setColumnValue(1, step_values.at(tick - 1));
            inserter->createRecord();
        }
    });

    auto query = db_mgr.createQuery("DataBlobs");
    EXPECT_EQUAL(query->count(), num_steps);

    query_time_t<TimeT> actual_time;
    query->select("Tick", actual_time);

    std::vector<size_t> actual_values;
    query->select("DataBlob", actual_values);

    auto results = query->getResultSet();
    std::reverse(step_values.begin(), step_values.end());
    while (!step_values.empty())
    {
        EXPECT_TRUE(results.getNextRecord());

        const TimeT expected_time = num_steps - step_values.size() + 1;
        EXPECT_EQUAL(expected_time, actual_time);

        const auto& expected_values = step_values.back();
        EXPECT_EQUAL(expected_values, actual_values);

        step_values.pop_back();
        ++tick;
    }

    EXPECT_FALSE(results.getNextRecord());
}

template <typename... TimeTs> void TestTimestampsFor()
{
    (TestTimestamps<TimeTs>(), ...);
}

int main()
{
    TestTimestampsFor<uint32_t, uint64_t, float, double>();

    REPORT_ERROR;
    return ERROR_CODE;
}
