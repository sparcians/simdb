// clang-format off

#include "SimDBTester.hpp"
#include "TestData.hpp"
#include "TestSchema.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <optional>

TEST_INIT;

/// This test covers basic SELECT functionality for SimDB.

int main()
{
    simdb::Schema schema;
    test::utils::defineTestSchema(schema);
    using dt = simdb::SqlDataType;

    simdb::DatabaseManager db_mgr("test.db", true);
    db_mgr.appendSchema(schema);

    // To get ready for testing the SqlQuery class, first create some new records.
    // Note that we don't test uint64_t here since there is already an exhaustive
    // test for that in test/sqlite/UInt64/main.cpp
    //
    // AllIntegerTypes
    // ---------------------------------------------------------------------------------
    // SomeInt32    SomeInt64    SomeUInt32
    // 111          555          404
    // 222          555          505
    // 333          555          606
    // 111          777          707
    // 222          777          808
    // 333          101          909
    auto insert_ints = [&](const int32_t i32, const int64_t i64, const uint32_t u32)
    {
        db_mgr.INSERT(
            SQL_TABLE("AllIntegerTypes"),
            SQL_COLUMNS("SomeInt32", "SomeInt64", "SomeUInt32"),
            SQL_VALUES(i32, i64, u32));
    };

    insert_ints((int32_t)111, (int64_t)555, (uint32_t)404);
    insert_ints((int32_t)222, (int64_t)555, (uint32_t)505);
    insert_ints((int32_t)333, (int64_t)555, (uint32_t)606);
    insert_ints((int32_t)111, (int64_t)777, (uint32_t)707);
    insert_ints((int32_t)222, (int64_t)777, (uint32_t)808);
    insert_ints((int32_t)333, (int64_t)101, (uint32_t)909);

    // FloatingPointTypes
    // ---------------------------------------------------------------------------------
    // SomeDouble
    // EPS
    // EPS
    // MIN
    // MIN
    // MAX
    // MAX
    // PI
    // PI
    // 1.0
    // 1.0
    // 0.3
    // 0.3
    auto insert_floats = [&](const double val)
    {
        db_mgr.INSERT(
            SQL_TABLE("FloatingPointTypes"),
            SQL_COLUMNS("SomeDouble"),
            SQL_VALUES(val));
    };

    for (auto val : {TEST_EPSILON, TEST_DOUBLE_MIN, TEST_DOUBLE_MAX, TEST_DOUBLE_PI, TEST_DOUBLE_EXACT, TEST_DOUBLE_INEXACT})
    {
        insert_floats(val);
        insert_floats(val);
    }

    // StringTypes
    // ---------------------------------------------------------------------------------
    // SomeString
    // foo
    // foo
    // bar
    // baz
    auto insert_strings = [&](const std::string& val)
    {
        db_mgr.INSERT(
            SQL_TABLE("StringTypes"),
            SQL_COLUMNS("SomeString"),
            SQL_VALUES(val));
    };

    insert_strings("foo");
    insert_strings("foo");
    insert_strings("bar");
    insert_strings("baz");

    // MixAndMatch
    // ---------------------------------------------------------------------------------
    // SomeInt32    SomeString    SomeBlob
    // 10           foo           TEST_VECTOR
    // 10           bar           TEST_VECTOR
    // 20           foo           TEST_VECTOR2
    // 20           bar           TEST_VECTOR2
    auto insert_mix_and_match = [&](const int32_t i32, const std::string& str, const std::vector<int>& blob)
    {
        db_mgr.INSERT(
            SQL_TABLE("MixAndMatch"),
            SQL_COLUMNS("SomeInt32", "SomeString", "SomeBlob"),
            SQL_VALUES(i32, str, blob));
    };

    insert_mix_and_match(10, "foo", TEST_VECTOR);
    insert_mix_and_match(10, "bar", TEST_VECTOR);
    insert_mix_and_match(20, "foo", TEST_VECTOR2);
    insert_mix_and_match(20, "bar", TEST_VECTOR2);

    // DefaultDoubles
    // ------------------------------------------------------------------------------------------------------
    // DefaultEPS    DefaultMIN       DefaultMAX       DefaultPI       DefaultEXACT       DefaultINEXACT
    // TEST_EPSILON  TEST_DOUBLE_MIN  TEST_DOUBLE_MAX  TEST_DOUBLE_PI  TEST_DOUBLE_EXACT  TEST_DOUBLE_INEXACT
    // TEST_EPSILON  TEST_DOUBLE_MIN  TEST_DOUBLE_MAX  TEST_DOUBLE_PI  TEST_DOUBLE_EXACT  TEST_DOUBLE_INEXACT
    db_mgr.INSERT(SQL_TABLE("DefaultDoubles"),
                  SQL_COLUMNS("DefaultEPS", "DefaultMIN", "DefaultMAX", "DefaultPI", "DefaultEXACT", "DefaultINEXACT"),
                  SQL_VALUES(TEST_EPSILON, TEST_DOUBLE_MIN, TEST_DOUBLE_MAX, TEST_DOUBLE_PI, TEST_DOUBLE_EXACT, TEST_DOUBLE_INEXACT));

    db_mgr.INSERT(SQL_TABLE("DefaultDoubles"),
                  SQL_COLUMNS("DefaultEPS", "DefaultMIN", "DefaultMAX", "DefaultPI", "DefaultEXACT", "DefaultINEXACT"),
                  SQL_VALUES(TEST_EPSILON, TEST_DOUBLE_MIN, TEST_DOUBLE_MAX, TEST_DOUBLE_PI, TEST_DOUBLE_EXACT, TEST_DOUBLE_INEXACT));

    db_mgr.safeTransaction(
        [&]()
        {
            // IndexedColumns
            // ------------------------------------------------------------------------------------------------------
            // SomeInt32    SomeDouble    SomeString
            // 1            1.1           "1.1"
            // 2            2.1           "2.1"
            // ...          ...           ...
            // 100000       100000.1      "100000.1"
            for (int idx = 1; idx <= 100000; ++idx)
            {
                auto val_int = idx;
                auto val_dbl = idx + 0.1;
                auto val_str = std::to_string(val_int);

                db_mgr.INSERT(SQL_TABLE("IndexedColumns"),
                              SQL_COLUMNS("SomeInt32", "SomeDouble", "SomeString"),
                              SQL_VALUES(val_int, val_dbl, val_str));
            }

            // NonIndexedColumns
            // ------------------------------------------------------------------------------------------------------
            // SomeInt32    SomeDouble    SomeString
            // 1            1.1           "1.1"
            // 2            2.1           "2.1"
            // ...          ...           ...
            // 100000       100000.1      "100000.1"
            for (int idx = 1; idx <= 100000; ++idx)
            {
                auto val_int = idx;
                auto val_dbl = idx + 0.1;
                auto val_str = std::to_string(val_int);

                db_mgr.INSERT(SQL_TABLE("NonIndexedColumns"),
                              SQL_COLUMNS("SomeInt32", "SomeDouble", "SomeString"),
                              SQL_VALUES(val_int, val_dbl, val_str));
            }
        });

    // Test SQL queries for integer types.
    int32_t i32;
    int64_t i64;
    uint32_t u32;

    auto query1 = db_mgr.createQuery("AllIntegerTypes");

    // Each successful call to result_set.getNextRecord() populates these variables.
    query1->select("SomeInt32", i32);
    query1->select("SomeInt64", i64);
    query1->select("SomeUInt32", u32);

    // SELECT COUNT(Id) should return 6 records.
    EXPECT_EQUAL(query1->count(), 6);
    {
        auto result_set = query1->getResultSet();

        // Iterate over the records one at a time and verify the data.
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 555);
        EXPECT_EQUAL(u32, 404);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 555);
        EXPECT_EQUAL(u32, 505);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 555);
        EXPECT_EQUAL(u32, 606);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 777);
        EXPECT_EQUAL(u32, 707);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 777);
        EXPECT_EQUAL(u32, 808);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 101);
        EXPECT_EQUAL(u32, 909);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());

        // Reset the iterator and make sure it can iterate again from the start.
        result_set.reset();
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_TRUE(result_set.getNextRecord());

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Add WHERE constraints, rerun the query, and check the results.
    query1->addConstraintForInt("SomeInt32", simdb::Constraints::NOT_EQUAL, 111);
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 777);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 101);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }
    query1->resetConstraints();

    query1->addConstraintForInt("SomeInt64", simdb::Constraints::EQUAL, 777);
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 777);
        EXPECT_EQUAL(u32, 707);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 777);
        EXPECT_EQUAL(u32, 808);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }
    query1->resetConstraints();

    query1->addConstraintForUInt32("SomeUInt32", simdb::Constraints::GREATER, 600);
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 555);
        EXPECT_EQUAL(u32, 606);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 777);
        EXPECT_EQUAL(u32, 707);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 777);
        EXPECT_EQUAL(u32, 808);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 101);
        EXPECT_EQUAL(u32, 909);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Remove WHERE constraints, add limit, rerun query.
    query1->resetConstraints();
    query1->setLimit(2);
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 555);
        EXPECT_EQUAL(u32, 404);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 555);
        EXPECT_EQUAL(u32, 505);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Add ORDER BY clauses, rerun query.
    query1->resetLimit();
    query1->orderBy("SomeInt32", simdb::QueryOrder::DESC);
    query1->orderBy("SomeInt64", simdb::QueryOrder::ASC);
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 101);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 777);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 777);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Test queries with NOT_EQUAL.
    query1->resetOrderBy();
    query1->addConstraintForInt("SomeInt32", simdb::Constraints::NOT_EQUAL, 222);
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 111);
        EXPECT_EQUAL(i64, 777);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 333);
        EXPECT_EQUAL(i64, 101);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Test queries with NOT IN clause.
    query1->resetConstraints();
    query1->addConstraintForInt("SomeInt32", simdb::SetConstraints::NOT_IN_SET, {111, 333});
    {
        auto result_set = query1->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 555);

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 222);
        EXPECT_EQUAL(i64, 777);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Test SQL queries for floating-point types. This use case is special since it requires
    // a custom comparator to deal with machine precision issues.
    auto query2 = db_mgr.createQuery("FloatingPointTypes");

    // Each successful call to result_set.getNextRecord() populates these variables.
    double dbl;
    query2->select("SomeDouble", dbl);

    // SELECT COUNT(Id) should return 12 records.
    EXPECT_EQUAL(query2->count(), 12);
    {
        auto result_set = query2->getResultSet();

        // Iterate over the records one at a time and verify the data.
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_EPSILON);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_EPSILON);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_MIN);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_MIN);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_MAX);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_MAX);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_PI);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_PI);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_EXACT);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_EXACT);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_INEXACT);
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_WITHIN_EPSILON(dbl, TEST_DOUBLE_INEXACT);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Check WHERE clauses for doubles.
    for (auto target : {TEST_EPSILON, TEST_DOUBLE_MIN, TEST_DOUBLE_MAX, TEST_DOUBLE_PI, TEST_DOUBLE_EXACT, TEST_DOUBLE_INEXACT})
    {
        // Not using fuzzyMatch() - test for equality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::EQUAL, target, false);
        EXPECT_EQUAL(query2->count(), 2);

        // Using fuzzyMatch() - test for equality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::EQUAL, target, true);
        EXPECT_EQUAL(query2->count(), 2);

        // Not using fuzzyMatch() - test the IN clause, test for equality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::SetConstraints::IN_SET, {target}, false);
        EXPECT_EQUAL(query2->count(), 2);

        // Using fuzzyMatch() - test the IN clause, test for equality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::SetConstraints::IN_SET, {target}, true);
        EXPECT_EQUAL(query2->count(), 2);

        // Not using fuzzyMatch() - test for inequality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::NOT_EQUAL, target, false);
        EXPECT_EQUAL(query2->count(), 10);

        // Using fuzzyMatch() - test for inequality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::NOT_EQUAL, target, true);
        EXPECT_EQUAL(query2->count(), 10);

        // Not using fuzzyMatch() - test the IN clause, test for inequality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::SetConstraints::NOT_IN_SET, {target}, false);
        EXPECT_EQUAL(query2->count(), 10);

        // Using fuzzyMatch() - test the IN clause, test for inequality.
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::SetConstraints::NOT_IN_SET, {target}, true);
        EXPECT_EQUAL(query2->count(), 10);
    }

    // Check WHERE clause for comparisons using <, <=, >, >=
    for (auto fuzzy : {false, true})
    {
        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::LESS, TEST_DOUBLE_PI, fuzzy);
        EXPECT_EQUAL(query2->count(), 8);

        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::LESS_EQUAL, TEST_DOUBLE_PI, fuzzy);
        EXPECT_EQUAL(query2->count(), 10);

        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::GREATER, TEST_DOUBLE_PI, fuzzy);
        EXPECT_EQUAL(query2->count(), 2);

        query2->resetConstraints();
        query2->addConstraintForDouble("SomeDouble", simdb::Constraints::GREATER_EQUAL, TEST_DOUBLE_PI, fuzzy);
        EXPECT_EQUAL(query2->count(), 4);
    }

    // Test queries against double targets, with and without fuzzyMatch().
    auto query3 = db_mgr.createQuery("DefaultDoubles");

    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultEPS", simdb::Constraints::EQUAL, TEST_EPSILON, false);
    EXPECT_EQUAL(query3->count(), 2);
    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultEPS", simdb::Constraints::EQUAL, TEST_EPSILON, true);
    EXPECT_EQUAL(query3->count(), 2);

    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultMIN", simdb::Constraints::EQUAL, TEST_DOUBLE_MIN, false);
    EXPECT_EQUAL(query3->count(), 2);
    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultMIN", simdb::Constraints::EQUAL, TEST_DOUBLE_MIN, true);
    EXPECT_EQUAL(query3->count(), 2);

    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultMAX", simdb::Constraints::EQUAL, TEST_DOUBLE_MAX, false);
    EXPECT_EQUAL(query3->count(), 2);
    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultMAX", simdb::Constraints::EQUAL, TEST_DOUBLE_MAX, true);
    EXPECT_EQUAL(query3->count(), 2);

    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultPI", simdb::Constraints::EQUAL, TEST_DOUBLE_PI, false);
    EXPECT_EQUAL(query3->count(), 2);
    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultPI", simdb::Constraints::EQUAL, TEST_DOUBLE_PI, true);
    EXPECT_EQUAL(query3->count(), 2);

    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultEXACT", simdb::Constraints::EQUAL, TEST_DOUBLE_EXACT, false);
    EXPECT_EQUAL(query3->count(), 2);
    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultEXACT", simdb::Constraints::EQUAL, TEST_DOUBLE_EXACT, true);
    EXPECT_EQUAL(query3->count(), 2);

    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultINEXACT", simdb::Constraints::EQUAL, TEST_DOUBLE_INEXACT, false);
    EXPECT_EQUAL(query3->count(), 2);
    query3->resetConstraints();
    query3->addConstraintForDouble("DefaultINEXACT", simdb::Constraints::EQUAL, TEST_DOUBLE_INEXACT, true);
    EXPECT_EQUAL(query3->count(), 2);

    // Test SQL queries for string types.
    auto query4 = db_mgr.createQuery("StringTypes");

    // Each successful call to result_set.getNextRecord() populates these variables.
    std::string str;
    query4->select("SomeString", str);

    // SELECT COUNT(Id) should return 4 records.
    EXPECT_EQUAL(query4->count(), 4);
    {
        auto result_set = query4->getResultSet();

        // Iterate over the records one at a time and verify the data.
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "foo");
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "foo");
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "bar");
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "baz");

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Add WHERE constraints, rerun the query, and check the results.
    query4->addConstraintForString("SomeString", simdb::Constraints::EQUAL, "foo");
    {
        auto result_set = query4->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "foo");
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "foo");

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    query4->resetConstraints();
    query4->addConstraintForString("SomeString", simdb::SetConstraints::IN_SET, {"bar", "baz"});
    query4->orderBy("SomeString", simdb::QueryOrder::DESC);
    {
        auto result_set = query4->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "baz");
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(str, "bar");

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    // MixAndMatch
    // ---------------------------------------------------------------------------------
    // SomeInt32    SomeString    SomeBlob
    // 10           foo           TEST_VECTOR
    // 10           bar           TEST_VECTOR
    // 20           foo           TEST_VECTOR2
    // 20           bar           TEST_VECTOR2

    // Test queries that include multiple kinds of data type constraints,
    // and which includes a blob column.
    auto query5 = db_mgr.createQuery("MixAndMatch");

    // Each successful call to result_set.getNextRecord() populates these variables.
    std::vector<int> ivec;
    query5->select("SomeInt32", i32);
    query5->select("SomeString", str);
    query5->select("SomeBlob", ivec);

    // SELECT COUNT(Id) should return 4 records.
    EXPECT_EQUAL(query5->count(), 4);

    query5->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 20);
    query5->addConstraintForString("SomeString", simdb::Constraints::EQUAL, "foo");
    {
        auto result_set = query5->getResultSet();

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 20);
        EXPECT_EQUAL(str, "foo");
        EXPECT_EQUAL(ivec, TEST_VECTOR2);

        // We should have read all the records.
        EXPECT_FALSE(result_set.getNextRecord());
    }

    auto query7 = db_mgr.createQuery("NonIndexedColumns");

    // Make sure the tables have the same number of records first.
    int NonIndexedColumns_id;
    query7->select("Id", NonIndexedColumns_id);
    query7->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 100000);
    query7->addConstraintForDouble("SomeDouble", simdb::Constraints::EQUAL, 100000.1);
    query7->addConstraintForString("SomeString", simdb::Constraints::EQUAL, "100000");

    // Ensure that we can connect a new DatabaseManager to a .db that was
    // created by another DatabaseManager.
    simdb::DatabaseManager db_mgr2(db_mgr.getDatabaseFilePath());

    int id;
    query7->select("Id", id);
    query7->select("SomeInt32", i32);
    query7->select("SomeDouble", dbl);
    query7->select("SomeString", str);

    {
        auto result_set = query7->getResultSet();
        EXPECT_TRUE(result_set.getNextRecord());
    }

    auto record12 = db_mgr2.findRecord("NonIndexedColumns", id);
    EXPECT_EQUAL(record12->getPropertyInt32("SomeInt32"), i32);
    EXPECT_EQUAL(record12->getPropertyDouble("SomeDouble"), dbl);
    EXPECT_EQUAL(record12->getPropertyString("SomeString"), str);

    // Ensure that we can append tables to an existing database schema.
    simdb::Schema schema2;

    schema2.addTable("AppendedTable").addColumn("SomeInt32", dt::int32_t);

    db_mgr.appendSchema(schema2);

    db_mgr.INSERT(SQL_TABLE("AppendedTable"), SQL_COLUMNS("SomeInt32"), SQL_VALUES(101));
    db_mgr.INSERT(SQL_TABLE("AppendedTable"), SQL_COLUMNS("SomeInt32"), SQL_VALUES(101));
    db_mgr.INSERT(SQL_TABLE("AppendedTable"), SQL_COLUMNS("SomeInt32"), SQL_VALUES(202));

    auto query8 = db_mgr.createQuery("AppendedTable");
    query8->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 101);
    EXPECT_EQUAL(query8->count(), 2);

    // Ensure that we can execute queries with OR clauses. Here we will test:
    //   SELECT COUNT(Id) FROM MixAndMatch WHERE (SomeInt32 = 10 AND SomeString = 'foo') OR (SomeString = 'foo')
    auto query9 = db_mgr.createQuery("MixAndMatch");

    query9->select("SomeInt32", i32);
    query9->select("SomeString", str);

    query9->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 10);
    query9->addConstraintForString("SomeString", simdb::Constraints::EQUAL, "foo");
    auto clause1 = query9->releaseConstraintClauses();

    query9->addConstraintForString("SomeString", simdb::Constraints::EQUAL, "foo");
    auto clause2 = query9->releaseConstraintClauses();

    query9->addCompoundConstraint(clause1, simdb::QueryOperator::OR, clause2);
    EXPECT_EQUAL(query9->count(), 2);

    {
        auto result_set = query9->getResultSet();
        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 10);
        EXPECT_EQUAL(str, "foo");

        EXPECT_TRUE(result_set.getNextRecord());
        EXPECT_EQUAL(i32, 20);
        EXPECT_EQUAL(str, "foo");

        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Verify that we cannot open a database connection for an invalid file
    EXPECT_THROW(simdb::DatabaseManager db_mgr3(__FILE__));

    // Test what happens when we delete records from a table using WHERE contraints.
    simdb::Schema schema3;
    auto& alphanum_tbl = schema3.addTable("AlphaNumbers");
    alphanum_tbl.addColumn("Alpha", dt::string_t);
    alphanum_tbl.addColumn("Number", dt::int32_t);
    db_mgr.appendSchema(schema3);

    for (int i = 1; i <= 26; ++i)
    {
        const auto letter = std::string(1, 'a'+i-1);
        const auto number = i;

        db_mgr.INSERT(
            SQL_TABLE("AlphaNumbers"),
            SQL_COLUMNS("Alpha", "Number"),
            SQL_VALUES(letter, number));
    }

    auto query10 = db_mgr.createQuery("AlphaNumbers");
    EXPECT_EQUAL(query10->count(), 26);
    query10->addConstraintForString("Alpha", simdb::SetConstraints::IN_SET, {"a","b","c","d","e"});

    // After deleting a-e, we should have 21 records left.
    query10->deleteResultSet();
    query10->resetConstraints();
    EXPECT_EQUAL(query10->count(), 21);

    // Now delete numbers 11-26. We should then have 5 records left.
    query10->addConstraintForInt("Number", simdb::Constraints::GREATER_EQUAL, 11);
    query10->deleteResultSet();
    query10->resetConstraints();
    EXPECT_EQUAL(query10->count(), 5);

    // Verify we are left with (f6-j10)
    std::vector<std::string> expected_alphas = {"f","g","h","i","j"};
    std::vector<int> expected_numbers = {6,7,8,9,10};

    std::string actual_alpha;
    query10->select("Alpha", actual_alpha);

    int actual_number;
    query10->select("Number", actual_number);

    {
        auto result_set = query10->getResultSet();
        for (size_t i = 0; i < 5; ++i)
        {
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_EQUAL(actual_alpha, expected_alphas[i]);
            EXPECT_EQUAL(actual_number, expected_numbers[i]);
        }

        EXPECT_FALSE(result_set.getNextRecord());
    }

    // Verify we can run a query without constraints on a table
    // that does not use auto-incrementing primary keys. This
    // verifies that a query "ORDER BY Id" bug is fixed (cannot
    // just assume that the Id column is there).
    simdb::Schema schema4;
    auto& customers_tbl = schema4.addTable("Customers");
    customers_tbl.addColumn("FirstName", dt::string_t);
    customers_tbl.addColumn("LastName", dt::string_t);
    customers_tbl.unsetPrimaryKey();

    db_mgr.appendSchema(schema4);

    db_mgr.INSERT(
        SQL_TABLE("Customers"),
        SQL_COLUMNS("FirstName", "LastName"),
        SQL_VALUES("Bob", "Smith"));

    db_mgr.INSERT(
        SQL_TABLE("Customers"),
        SQL_COLUMNS("FirstName", "LastName"),
        SQL_VALUES("Jane", "Smith"));

    auto query11 = db_mgr.createQuery("Customers");

    std::string customer_first_name;
    query11->select("FirstName", customer_first_name);

    auto result_set = query11->getResultSet();
    EXPECT_TRUE(result_set.getNextRecord());
    EXPECT_EQUAL(customer_first_name, "Bob");
    EXPECT_TRUE(result_set.getNextRecord());
    EXPECT_EQUAL(customer_first_name, "Jane");
    EXPECT_FALSE(result_set.getNextRecord());

    // Test std::optional SELECT overloads for NULL/unset column values.
    {
        simdb::Schema optional_schema;
        auto& optional_tbl = optional_schema.addTable("OptionalTypes");
        optional_tbl.addColumn("SomeInt32", dt::int32_t);
        optional_tbl.addColumn("SomeUInt32", dt::uint32_t);
        optional_tbl.addColumn("SomeInt64", dt::int64_t);
        optional_tbl.addColumn("SomeUInt64", dt::uint64_t);
        optional_tbl.addColumn("SomeDouble", dt::double_t);
        db_mgr.appendSchema(optional_schema);

        db_mgr.INSERT(SQL_TABLE("OptionalTypes"), SQL_COLUMNS("SomeInt32"), SQL_VALUES((int32_t)42));

        db_mgr.INSERT(
            SQL_TABLE("OptionalTypes"),
            SQL_COLUMNS("SomeInt32", "SomeUInt32", "SomeInt64", "SomeUInt64", "SomeDouble"),
            SQL_VALUES((int32_t)100, (uint32_t)200, (int64_t)300, (uint64_t)400, TEST_DOUBLE_PI));

        db_mgr.INSERT(SQL_TABLE("OptionalTypes"), SQL_COLUMNS("SomeUInt64"), SQL_VALUES((uint64_t)UINT64_MAX));

        auto optional_query = db_mgr.createQuery("OptionalTypes");

        std::optional<int32_t> opt_i32;
        std::optional<uint32_t> opt_u32;
        std::optional<int64_t> opt_i64;
        std::optional<uint64_t> opt_u64;
        std::optional<double> opt_dbl;

        optional_query->select("SomeInt32", opt_i32);
        optional_query->select("SomeUInt32", opt_u32);
        optional_query->select("SomeInt64", opt_i64);
        optional_query->select("SomeUInt64", opt_u64);
        optional_query->select("SomeDouble", opt_dbl);
        optional_query->orderBy("Id", simdb::QueryOrder::ASC);

        EXPECT_EQUAL(optional_query->count(), 3);
        {
            auto result_set = optional_query->getResultSet();

            // Row 1: only SomeInt32 is set.
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_TRUE(opt_i32.has_value());
            EXPECT_EQUAL(opt_i32.value(), 42);
            EXPECT_FALSE(opt_u32.has_value());
            EXPECT_FALSE(opt_i64.has_value());
            EXPECT_FALSE(opt_u64.has_value());
            EXPECT_FALSE(opt_dbl.has_value());

            // Row 2: all columns set.
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_TRUE(opt_i32.has_value());
            EXPECT_EQUAL(opt_i32.value(), 100);
            EXPECT_TRUE(opt_u32.has_value());
            EXPECT_EQUAL(opt_u32.value(), 200u);
            EXPECT_TRUE(opt_i64.has_value());
            EXPECT_EQUAL(opt_i64.value(), 300);
            EXPECT_TRUE(opt_u64.has_value());
            EXPECT_EQUAL(opt_u64.value(), 400u);
            EXPECT_TRUE(opt_dbl.has_value());
            const double actual_dbl = opt_dbl.value();
            EXPECT_WITHIN_EPSILON(actual_dbl, TEST_DOUBLE_PI);

            // Row 3: only SomeUInt64 is set (NULL columns must not throw).
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_FALSE(opt_i32.has_value());
            EXPECT_FALSE(opt_u32.has_value());
            EXPECT_FALSE(opt_i64.has_value());
            EXPECT_TRUE(opt_u64.has_value());
            EXPECT_EQUAL(opt_u64.value(), UINT64_MAX);
            EXPECT_FALSE(opt_dbl.has_value());

            EXPECT_FALSE(result_set.getNextRecord());

            // Verify reset() reuses optional writers via clone().
            result_set.reset();
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_TRUE(opt_i32.has_value());
            EXPECT_EQUAL(opt_i32.value(), 42);
            EXPECT_FALSE(opt_u64.has_value());
        }

        // Non-optional SELECT throws on NULL columns instead of coercing to zero.
        {
            auto plain_query = db_mgr.createQuery("OptionalTypes");
            uint32_t u32 = 999;
            plain_query->select("SomeUInt32", u32);
            plain_query->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 42);

            auto result_set = plain_query->getResultSet();
            EXPECT_THROW(result_set.getNextRecord());
        }
        {
            auto plain_query = db_mgr.createQuery("OptionalTypes");
            uint64_t u64 = 999;
            plain_query->select("SomeUInt64", u64);
            plain_query->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 42);

            auto result_set = plain_query->getResultSet();
            EXPECT_THROW(result_set.getNextRecord());
        }
        {
            auto plain_query = db_mgr.createQuery("OptionalTypes");
            uint32_t u32 = 0;
            plain_query->select("SomeUInt32", u32);
            plain_query->addConstraintForInt("SomeInt32", simdb::Constraints::EQUAL, 100);

            auto result_set = plain_query->getResultSet();
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_EQUAL(u32, 200u);
            EXPECT_FALSE(result_set.getNextRecord());
        }
    }

    // Test std::optional<std::string> to distinguish SQL NULL from "".
    {
        simdb::Schema optional_string_schema;
        optional_string_schema.addTable("OptionalStrings")
            .addColumn("Label", dt::int32_t)
            .addColumn("SomeString", dt::string_t);
        db_mgr.appendSchema(optional_string_schema);

        db_mgr.INSERT(SQL_TABLE("OptionalStrings"), SQL_COLUMNS("Label"), SQL_VALUES((int32_t)1));
        db_mgr.INSERT(SQL_TABLE("OptionalStrings"), SQL_COLUMNS("Label", "SomeString"), SQL_VALUES((int32_t)2, ""));
        db_mgr.INSERT(SQL_TABLE("OptionalStrings"), SQL_COLUMNS("Label", "SomeString"), SQL_VALUES((int32_t)3, "hello"));

        int32_t label = 0;

        // NULL columns are written as "" for plain std::string.
        {
            auto plain_query = db_mgr.createQuery("OptionalStrings");
            std::string str;
            plain_query->select("Label", label);
            plain_query->select("SomeString", str);
            plain_query->addConstraintForInt("Label", simdb::Constraints::EQUAL, 1);

            auto result_set = plain_query->getResultSet();
            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_EQUAL(label, 1);
            EXPECT_EQUAL(str, "");
            EXPECT_FALSE(result_set.getNextRecord());
        }

        // std::optional distinguishes SQL NULL from "".
        {
            auto opt_query = db_mgr.createQuery("OptionalStrings");
            std::optional<std::string> opt_str;
            opt_query->select("Label", label);
            opt_query->select("SomeString", opt_str);
            opt_query->orderBy("Label", simdb::QueryOrder::ASC);

            EXPECT_EQUAL(opt_query->count(), 3);
            auto result_set = opt_query->getResultSet();

            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_EQUAL(label, 1);
            EXPECT_FALSE(opt_str.has_value());

            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_EQUAL(label, 2);
            EXPECT_TRUE(opt_str.has_value());
            EXPECT_EQUAL(opt_str.value(), "");

            EXPECT_TRUE(result_set.getNextRecord());
            EXPECT_EQUAL(label, 3);
            EXPECT_TRUE(opt_str.has_value());
            EXPECT_EQUAL(opt_str.value(), "hello");

            EXPECT_FALSE(result_set.getNextRecord());
        }
    }

    REPORT_ERROR;
    return ERROR_CODE;
}
