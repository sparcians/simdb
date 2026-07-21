// clang-format off

#include "SimDBTester.hpp"
#include "simdb/apps/App.hpp"
#include "simdb/apps/AppManager.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/apps/argos/ArgosCollector.hpp"

TEST_INIT;

enum class EnumNoOStream : uint16_t
{
    ONE = 1,
    TWO,
    THREE
};

enum class EnumWithOStream : uint32_t
{
    ONE = 1,
    TWO,
    THREE
};

std::ostream& operator<<(std::ostream& os, EnumWithOStream e)
{
    switch (e)
    {
        case EnumWithOStream::ONE:   return os << "ONE";
        case EnumWithOStream::TWO:   return os << "TWO";
        case EnumWithOStream::THREE: return os << "THREE";
        default: throw simdb::DBException("Invalid enum");
    }
    return os;
}

int main(int argc, char** argv)
{
    simdb::AppManagers app_mgrs;
    app_mgrs.registerApp<simdb::argos::ArgosCollector>();
    app_mgrs.setVerbose(true);

    // Create the app/db managers
    auto& app_mgr = app_mgrs.createAppManager("test.db");
    auto& db_mgr = app_mgrs.getDatabaseManager();

    // Setup...
    app_mgr.enableApp(simdb::argos::ArgosCollector::NAME);
    app_mgrs.createEnabledApps();
    app_mgrs.createSchemas();
    app_mgrs.postInit(argc, argv);
    app_mgrs.initializePipelines();
    app_mgrs.openPipelines();

    auto app = app_mgr.getApp<simdb::argos::ArgosCollector>();
    uint64_t tick = 0;
    app->timestampWith(&tick);
    app->addClock("root", 1 /*period*/);

    // Simulate...
    auto tiny_strings = app->getTinyStrings();
    auto foo_sid = tiny_strings->getStringID("foo");
    auto bar_sid = tiny_strings->getStringID("bar");

    auto enum_inspector = app->getEnumInspector();
    enum_inspector->inspect(EnumNoOStream::ONE);
    enum_inspector->inspect(EnumNoOStream::TWO);
    enum_inspector->inspect(EnumNoOStream::THREE);
    enum_inspector->inspect(EnumWithOStream::ONE);
    enum_inspector->inspect(EnumWithOStream::TWO);
    enum_inspector->inspect(EnumWithOStream::THREE);

    // Finalize...
    app_mgrs.postSimLoopTeardown();

    // Validate TinyStrings...
    auto tiny_strings_query = db_mgr.createQuery("TinyStringIDs");

    std::string str_val;
    tiny_strings_query->select("StringValue", str_val);

    uint32_t str_id;
    tiny_strings_query->select("StringID", str_id);

    tiny_strings_query->orderBy("StringID", simdb::QueryOrder::ASC);
    auto tiny_strings_results = tiny_strings_query->getResultSet();

    EXPECT_TRUE(tiny_strings_results.getNextRecord());
    EXPECT_EQUAL(str_val, "foo");
    EXPECT_EQUAL(str_id, foo_sid);

    EXPECT_TRUE(tiny_strings_results.getNextRecord());
    EXPECT_EQUAL(str_val, "bar");
    EXPECT_EQUAL(str_id, bar_sid);

    EXPECT_FALSE(tiny_strings_results.getNextRecord());

    // Validate enums...
    auto enums_query = db_mgr.createQuery("CollectedEnums");

    // No entry for enums without a stream operator
    enums_query->addConstraintForString("EnumName", simdb::Constraints::EQUAL, "EnumNoOStream");
    EXPECT_EQUAL(enums_query->count(), 0);

    // Now verify EnumWithOStream
    enums_query->resetConstraints();
    enums_query->addConstraintForString("EnumName", simdb::Constraints::EQUAL, "EnumWithOStream");

    int enum_id;
    enums_query->select("Id", enum_id);

    auto enums_results = enums_query->getResultSet();
    EXPECT_TRUE(enums_results.getNextRecord());
    EXPECT_FALSE(enums_results.getNextRecord());
    EXPECT_EQUAL(enum_id, 1);

    auto enum_members_query = db_mgr.createQuery("EnumMembers");
    enum_members_query->addConstraintForInt("EnumID", simdb::Constraints::EQUAL, enum_id);

    std::string member_name;
    enum_members_query->select("MemberName", member_name);

    std::string member_val_str;
    enum_members_query->select("MemberValueStr", member_val_str);

    auto enum_members_results = enum_members_query->getResultSet();

    EXPECT_TRUE(enum_members_results.getNextRecord());
    EXPECT_EQUAL(member_name, "ONE");
    EXPECT_EQUAL(member_val_str, "1");

    EXPECT_TRUE(enum_members_results.getNextRecord());
    EXPECT_EQUAL(member_name, "TWO");
    EXPECT_EQUAL(member_val_str, "2");

    EXPECT_TRUE(enum_members_results.getNextRecord());
    EXPECT_EQUAL(member_name, "THREE");
    EXPECT_EQUAL(member_val_str, "3");

    EXPECT_FALSE(enum_members_results.getNextRecord());

    REPORT_ERROR;
    return ERROR_CODE;
}
