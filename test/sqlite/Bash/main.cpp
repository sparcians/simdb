#include "simdb/sqlite/DatabaseManager.hpp"
#include "SimDBTester.hpp"

TEST_INIT;

/// This test bashes concurrent use of DatabaseManager::safeTransaction/INSERT
/// for DB access safety (locked tables etc. should not be a show-stopper and
/// should be able to recover).

void initSchema(simdb::DatabaseManager& db_mgr)
{
    simdb::Schema schema;
    auto& tbl = schema.addTable("AllTheData");

    using dt = simdb::SqlDataType;
    tbl.addColumn("SomeString", dt::string_t);
    tbl.addColumn("SomeBlob", dt::blob_t);

    db_mgr.appendSchema(schema);
}

class Runner
{
public:
    Runner(simdb::DatabaseManager& db_mgr) : db_mgr_(db_mgr) {}
    Runner(const Runner&) = default;
    Runner(Runner&&) = default;

    ~Runner()
    {
        close();
    }

    void open()
    {
        if (!thread_)
        {
            stop_requested_ = false;
            thread_ = std::make_unique<std::thread>(&Runner::loop_, this);
        }
    }

    void close()
    {
        if (thread_)
        {
            stop_requested_ = true;
            if (thread_->joinable())
            {
                thread_->join();
            }
            thread_.reset();
        }
    }

    void setNextSleepTime(size_t ms)
    {
        next_sleep_ms_ = ms;
    }

private:
    void loop_()
    {
        while (!stop_requested_)
        {
            doRandomInsert_();
            sleep_();
        }
    }

    void doRandomInsert_()
    {
        static const std::string string = "blah_blah_blah_blah_blah_blah_blah_blah";
        static const std::array<uint32_t, 10000> array{0};
        const simdb::SqlBlob blob(array);

        db_mgr_.INSERT(
            SQL_TABLE("AllTheData"),
            SQL_VALUES(string, blob));
    }

    void sleep_() const
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(next_sleep_ms_));
    }

    simdb::DatabaseManager& db_mgr_;
    std::unique_ptr<std::thread> thread_;
    bool stop_requested_ = false;
    size_t next_sleep_ms_ = 10;
};

int main()
{
    constexpr auto new_file = true;
    simdb::DatabaseManager db_mgr("test.db", new_file);
    initSchema(db_mgr);

    constexpr auto num_runners = 10u;
    std::vector<std::unique_ptr<Runner>> runners;
    for (size_t i = 0; i < num_runners; ++i)
    {
        runners.emplace_back(std::make_unique<Runner>(db_mgr));
    }

    for (auto& runner : runners)
    {
        runner->open();
    }

    const std::vector<size_t> sleep_ms = {1, 5, 10, 25, 50, 100};
    for (auto ms : sleep_ms)
    {
        std::cout << "Checking with DB backoff of " << ms << "ms" << std::endl;
        for (auto& runner : runners)
        {
            runner->setNextSleepTime(ms);
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    for (auto& runner : runners)
    {
        runner->close();
    }

    REPORT_ERROR;
    return ERROR_CODE;
}
