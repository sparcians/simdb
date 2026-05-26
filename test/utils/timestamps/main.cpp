#include "SimDBTester.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <random>
std::random_device rd;  // Seed source for the random number engine
std::mt19937 gen(rd()); // mersenne_twister_engine

/// This test covers basic functionality of the Timestamp utility.
TEST_INIT;

void TestTimestamps()
{
    using namespace simdb::argos;

    uint64_t tick = 0;
    Timestamp timestamp(&tick);

    simdb::DatabaseManager db_mgr("test.db", true /*new file*/);

    simdb::Schema schema;
    using dt = simdb::SqlDataType;

    auto& tbl = schema.addTable("DataBlobs");
    tbl.addColumn("Tick", dt::uint64_t);
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
            inserter->setColumnValue(0, timestamp.getTime());
            inserter->setColumnValue(1, step_values.at(tick - 1));
            inserter->createRecord();
        }
    });

    auto query = db_mgr.createQuery("DataBlobs");
    EXPECT_EQUAL(query->count(), num_steps);

    uint64_t actual_time;
    query->select("Tick", actual_time);

    std::vector<size_t> actual_values;
    query->select("DataBlob", actual_values);

    auto results = query->getResultSet();
    std::reverse(step_values.begin(), step_values.end());
    while (!step_values.empty())
    {
        EXPECT_TRUE(results.getNextRecord());

        const uint64_t expected_time = num_steps - step_values.size() + 1;
        EXPECT_EQUAL(expected_time, actual_time);

        const auto& expected_values = step_values.back();
        EXPECT_EQUAL(expected_values, actual_values);

        step_values.pop_back();
        ++tick;
    }

    EXPECT_FALSE(results.getNextRecord());
}

int main()
{
    TestTimestamps();

    REPORT_ERROR;
    return ERROR_CODE;
}
