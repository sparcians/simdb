#include "SimDBTester.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/utils/Compress.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <atomic>
#include <thread>

TEST_INIT;

// The SimDB utils are all implicitly tested in the tests/examples already.
// Extra tests are added here for edge cases and full code coverage.

void TestCompress()
{
    std::vector<double> uncompressed;
    std::vector<char> compressed;

    simdb::compressData(uncompressed, compressed);
    EXPECT_EQUAL(compressed.size(), 0);

    simdb::decompressData(compressed, uncompressed);
    EXPECT_EQUAL(uncompressed.size(), 0);
}

void TestConcurrentQueue()
{
    simdb::ConcurrentQueue<int> queue;
    EXPECT_EQUAL(queue.size(), 0);

    simdb::ConcurrentQueue<simdb::argos::NotifEntry> notif_queue;
    simdb::argos::NotifEntry notif(nullptr, "some-notification", simdb::argos::NotifType::MESSAGE);
    notif_queue.emplace(std::move(notif));
    EXPECT_EQUAL(notif.notif, "");
    notif_queue.try_pop(notif);
    EXPECT_EQUAL(notif.notif, "some-notification");
}

void TestDemangle()
{
    const std::string bad_type = "not-a-real-data-type";
    const auto demangled = simdb::demangle(bad_type);
    EXPECT_EQUAL(demangled, bad_type);
}

void TestTinyStrings()
{
    simdb::TinyStrings<> tiny_strings;
    const std::string cpp_string;
    EXPECT_EQUAL(tiny_strings.getStringID(cpp_string), simdb::detail::BAD_STRING_ID);

    tiny_strings.getStringID("one");
    tiny_strings.getStringID("two");
    tiny_strings.getStringID("three");

    simdb::DatabaseManager db_mgr("test.db", true);
    tiny_strings.serialize(&db_mgr);

    auto query = db_mgr.createQuery("TinyStringIDs");
    EXPECT_EQUAL(query->count(), 3);

    tiny_strings.serialize(&db_mgr);
    EXPECT_EQUAL(query->count(), 3);

    simdb::DatabaseManager db_mgr2("test2.db", true);
    EXPECT_THROW(tiny_strings.serialize(&db_mgr2));
}

void TestValidValue()
{
    simdb::ValidValue<int> vv;
    EXPECT_THROW(vv.getValue());
}

void TestSafeTransaction()
{
    class Tester
    {
    public:
        explicit Tester(const std::string& db_file = "test.db") :
            db_mgr_(db_file, true)
        {
            simdb::Schema schema;
            auto& tbl = schema.addTable("TheNumbers");
            tbl.addColumn("TheNumber", simdb::SqlDataType::int32_t);
            db_mgr_.appendSchema(schema);
            inserter_ = db_mgr_.prepareINSERT(SQL_TABLE("TheNumbers"));

            // A second connection to the same file, used by the test thread
            // to contend with the long-running transaction on db_mgr_.
            contender_db_mgr_ = std::make_unique<simdb::DatabaseManager>(db_file, false);
            contender_inserter_ = contender_db_mgr_->prepareINSERT(SQL_TABLE("TheNumbers"));
        }

        void startLongRunningINSERT()
        {
            simdb_assert(thread_ == nullptr);
            simdb_assert(stop_requested_ == false);
            thread_ = std::make_unique<std::thread>(&Tester::longRunningINSERT_, this);
        }

        void stopLongRunningINSERT()
        {
            if (thread_)
            {
                stop_requested_ = true;
                thread_->join();
                thread_.reset();
                stop_requested_ = false;
            }
        }

        void tryToLockUpDatabase()
        {
            simdb_assert(thread_ != nullptr);
            simdb_assert(stop_requested_ == false);

            size_t num_tries = 0;
            while (num_tries++ < 10)
            {
                if (!in_transaction_)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                } else
                {
                    break;
                }
            }

            auto start = std::chrono::high_resolution_clock::now();
            auto timed_out = [&](uint64_t seconds = 5) {
                auto end = std::chrono::high_resolution_clock::now();
                auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                auto msec = static_cast<uint64_t>(dur.count());
                return msec > seconds * 1000;
            };

            simdb_assert(in_transaction_);
            while (!timed_out())
            {
                // Skip safeTransaction() here since that would block
                // on the transaction mutex and would not surface a
                // locked DB issue.
                try
                {
                    contender_inserter_->createRecordWithColValues(rand());
                } catch (const simdb::SafeTransactionSilentException&)
                {
                    // Lockup reached.
                    success_ = true;
                    break;
                } catch (...)
                {
                    // Other exceptions are not expected.
                    break;
                }
            }

            stopLongRunningINSERT();
            EXPECT_TRUE(success_);
        }

    private:
        void longRunningINSERT_()
        {
            struct RAII
            {
                RAII(std::atomic<bool>& b) :
                    b_(b)
                {
                }
                ~RAII() { b_ = false; }
                void set() { b_ = true; }
                std::atomic<bool>& b_;
            };
            [[maybe_unused]] RAII raii(in_transaction_);

            db_mgr_.safeTransaction([&]() {
                raii.set();
                while (!stop_requested_)
                {
                    doINSERT_();
                }
            });
        }

        void doINSERT_() { inserter_->createRecordWithColValues(rand()); }

        simdb::DatabaseManager db_mgr_;
        std::unique_ptr<simdb::DatabaseManager> contender_db_mgr_;
        std::unique_ptr<std::thread> thread_;
        std::unique_ptr<simdb::PreparedINSERT> inserter_;
        std::unique_ptr<simdb::PreparedINSERT> contender_inserter_;
        std::atomic<bool> stop_requested_{false};
        std::atomic<bool> in_transaction_{false};
        bool success_ = false;
    };

    Tester tester;
    tester.startLongRunningINSERT();
    tester.tryToLockUpDatabase();
}

int main()
{
    TestCompress();
    TestConcurrentQueue();
    TestDemangle();
    TestTinyStrings();
    TestValidValue();
    TestSafeTransaction();

    REPORT_ERROR;
    return ERROR_CODE;
}
