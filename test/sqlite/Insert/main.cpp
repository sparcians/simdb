// clang-format off

#include "SimDBTester.hpp"
#include "TestData.hpp"
#include "TestSchema.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

TEST_INIT;

/// This test covers basic INSERT functionality for SimDB.

int main()
{
    simdb::Schema schema;
    test::utils::defineTestSchema(schema);

    simdb::DatabaseManager db_mgr("test.db", true);
    db_mgr.appendSchema(schema);

    // Verify INSERT for integer types
    auto record1 = db_mgr.INSERT(
        SQL_TABLE("SignedIntegerTypes"),
        SQL_COLUMNS("SomeInt32", "SomeInt64"),
        SQL_VALUES(TEST_INT32, TEST_INT64));

    EXPECT_EQUAL(record1->getPropertyInt32("SomeInt32"), TEST_INT32);
    EXPECT_EQUAL(record1->getPropertyInt64("SomeInt64"), TEST_INT64);

    record1->setPropertyInt32("SomeInt32", TEST_INT32 / 2);
    EXPECT_EQUAL(record1->getPropertyInt32("SomeInt32"), TEST_INT32 / 2);

    record1->setPropertyInt64("SomeInt64", TEST_INT64 / 2);
    EXPECT_EQUAL(record1->getPropertyInt64("SomeInt64"), TEST_INT64 / 2);

    // Verify INSERT(table, SQL_VALUES(...)) overload: all columns in schema order, no SQL_COLUMNS.
    auto record_all_cols = db_mgr.INSERT(
        SQL_TABLE("AllIntegerTypes"),
        SQL_VALUES(TEST_INT32, TEST_INT64, static_cast<uint32_t>(TEST_INT32), static_cast<uint64_t>(TEST_INT64)));
    EXPECT_EQUAL(record_all_cols->getPropertyInt32("SomeInt32"), TEST_INT32);
    EXPECT_EQUAL(record_all_cols->getPropertyInt64("SomeInt64"), TEST_INT64);
    EXPECT_EQUAL(record_all_cols->getPropertyUInt32("SomeUInt32"), static_cast<uint32_t>(TEST_INT32));
    EXPECT_EQUAL(record_all_cols->getPropertyUInt64("SomeUInt64"), static_cast<uint64_t>(TEST_INT64));

    // Verify INSERT for floating-point types
    auto record2 = db_mgr.INSERT(
        SQL_TABLE("FloatingPointTypes"),
        SQL_COLUMNS("SomeDouble"),
        SQL_VALUES(TEST_DOUBLE));

    EXPECT_EQUAL(record2->getPropertyDouble("SomeDouble"), TEST_DOUBLE);

    record2->setPropertyDouble("SomeDouble", TEST_DOUBLE / 2);
    EXPECT_EQUAL(record2->getPropertyDouble("SomeDouble"), TEST_DOUBLE / 2);

    // Verify INSERT for string types
    auto record3 = db_mgr.INSERT(
        SQL_TABLE("StringTypes"),
        SQL_COLUMNS("SomeString"),
        SQL_VALUES(TEST_STRING));

    EXPECT_EQUAL(record3->getPropertyString("SomeString"), TEST_STRING);

    record3->setPropertyString("SomeString", TEST_STRING + "2");
    EXPECT_EQUAL(record3->getPropertyString("SomeString"), TEST_STRING + "2");

    // Verify INSERT for blob types
    auto record4 = db_mgr.INSERT(
        SQL_TABLE("BlobTypes"),
        SQL_COLUMNS("SomeBlob"),
        SQL_VALUES(TEST_VECTOR));

    EXPECT_EQUAL(record4->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR);

    record4->setPropertyBlob("SomeBlob", TEST_VECTOR2);
    EXPECT_EQUAL(record4->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR2);

    auto record5 = db_mgr.INSERT(
        SQL_TABLE("BlobTypes"),
        SQL_COLUMNS("SomeBlob"),
        SQL_VALUES(TEST_BLOB));

    EXPECT_EQUAL(record5->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR);

    record5->setPropertyBlob("SomeBlob", TEST_BLOB2.data_ptr, TEST_BLOB2.num_bytes);
    EXPECT_EQUAL(record5->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR2);

    simdb::SqlBlob blob(TEST_VECTOR);
    {
        auto record6 = db_mgr.INSERT(
            SQL_TABLE("BlobTypes"),
            SQL_COLUMNS("SomeBlob"),
            SQL_VALUES(blob));

        EXPECT_EQUAL(record6->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR);
    }

    // Verify that bug is fixed: SQL_VALUES(..., <blob column>, ...)
    // would not compile when a blob (or vector) value was used in the
    // middle the SQL_VALUES (or anywhere but the last supplied value).
    db_mgr.INSERT(
        SQL_TABLE("MixAndMatch"),
        SQL_COLUMNS("SomeBlob", "SomeString"),
        SQL_VALUES(TEST_VECTOR, "foo"));

    db_mgr.INSERT(
        SQL_TABLE("MixAndMatch"),
        SQL_COLUMNS("SomeInt32", "SomeBlob", "SomeString"),
        SQL_VALUES(10, TEST_BLOB, "foo"));

    // Verify that we can INSERT vector blobs with move semantics
    std::vector<int> vec_to_move = TEST_VECTOR2;
    auto record7 = db_mgr.INSERT(
        SQL_TABLE("BlobTypes"),
        SQL_COLUMNS("SomeBlob"),
        SQL_VALUES(std::move(vec_to_move)));

    EXPECT_EQUAL(record7->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR2);
    EXPECT_TRUE(vec_to_move.empty());

    // Verify setDefaultValue()
    auto record6 = db_mgr.INSERT(SQL_TABLE("DefaultValues"));
    EXPECT_EQUAL(record6->getPropertyInt32("DefaultInt32"), TEST_INT32);
    EXPECT_EQUAL(record6->getPropertyInt64("DefaultInt64"), TEST_INT64);
    EXPECT_EQUAL(record6->getPropertyString("DefaultString"), TEST_STRING);
    EXPECT_WITHIN_EPSILON(record6->getPropertyDouble("DefaultDouble"), TEST_DOUBLE);

    // Verify the PreparedINSERT class
    simdb::Schema schema2;
    using dt = simdb::SqlDataType;

    auto& high_volume_data_tbl = schema2.addTable("HighVolumeBlobs");
    high_volume_data_tbl.addColumn("StartTick", dt::uint32_t);
    high_volume_data_tbl.addColumn("EndTick", dt::uint32_t);
    high_volume_data_tbl.addColumn("DataBlob", dt::blob_t);
    db_mgr.appendSchema(schema2);

    auto high_volume_insert = db_mgr.prepareINSERT(
        SQL_TABLE("HighVolumeBlobs"),
        SQL_COLUMNS("StartTick", "EndTick", "DataBlob"));

    auto data_vec = TEST_VECTOR;
    high_volume_insert->setColumnValue(0, 100u);
    high_volume_insert->setColumnValue(1, UINT32_MAX / 2);
    high_volume_insert->setColumnValue(2, data_vec);

    auto high_volume_record_id1 = high_volume_insert->createRecord();

    data_vec.front() = 123;
    data_vec.back() = 456;
    high_volume_insert->setColumnValue(0, UINT32_MAX / 2 + 1);
    high_volume_insert->setColumnValue(1, UINT32_MAX);
    high_volume_insert->setColumnValue(2, data_vec);

    auto high_volume_record_id2 = high_volume_insert->createRecord();

    auto high_volume_record1 = db_mgr.findRecord("HighVolumeBlobs", high_volume_record_id1);
    EXPECT_EQUAL(high_volume_record1->getPropertyUInt32("StartTick"), 100);
    EXPECT_EQUAL(high_volume_record1->getPropertyUInt32("EndTick"), UINT32_MAX / 2);
    EXPECT_EQUAL(high_volume_record1->getPropertyBlob<int>("DataBlob"), TEST_VECTOR);

    auto high_volume_record2 = db_mgr.findRecord("HighVolumeBlobs", high_volume_record_id2);
    EXPECT_EQUAL(high_volume_record2->getPropertyUInt32("StartTick"), UINT32_MAX / 2 + 1);
    EXPECT_EQUAL(high_volume_record2->getPropertyUInt32("EndTick"), UINT32_MAX);
    EXPECT_EQUAL(high_volume_record2->getPropertyBlob<int>("DataBlob"), data_vec);

    // Verify prepareINSERT(SqlTable) overload: all columns from schema, no SQL_COLUMNS.
    auto all_cols_insert = db_mgr.prepareINSERT(SQL_TABLE("AllIntegerTypes"));
    all_cols_insert->setColumnValue(0, TEST_INT32);
    all_cols_insert->setColumnValue(1, TEST_INT64);
    all_cols_insert->setColumnValue(2, static_cast<uint32_t>(TEST_INT32));
    all_cols_insert->setColumnValue(3, static_cast<uint64_t>(TEST_INT64));
    auto all_cols_id = all_cols_insert->createRecord();
    auto all_cols_record = db_mgr.findRecord("AllIntegerTypes", all_cols_id);
    EXPECT_EQUAL(all_cols_record->getPropertyInt32("SomeInt32"), TEST_INT32);
    EXPECT_EQUAL(all_cols_record->getPropertyInt64("SomeInt64"), TEST_INT64);
    EXPECT_EQUAL(all_cols_record->getPropertyUInt32("SomeUInt32"), static_cast<uint32_t>(TEST_INT32));
    EXPECT_EQUAL(all_cols_record->getPropertyUInt64("SomeUInt64"), static_cast<uint64_t>(TEST_INT64));

    // Verify createRecordWithColValues(): single call with all column values (single- and multi-column).
    auto float_inserter = db_mgr.prepareINSERT(SQL_TABLE("FloatingPointTypes"));
    auto float_id = float_inserter->createRecordWithColValues(TEST_DOUBLE);
    auto float_rec = db_mgr.findRecord("FloatingPointTypes", float_id);
    EXPECT_EQUAL(float_rec->getPropertyDouble("SomeDouble"), TEST_DOUBLE);

    auto one_shot_inserter = db_mgr.prepareINSERT(SQL_TABLE("AllIntegerTypes"));
    const uint32_t u32_val = static_cast<uint32_t>(TEST_INT32);
    const uint64_t u64_val = static_cast<uint64_t>(TEST_INT64);
    auto one_shot_id = one_shot_inserter->createRecordWithColValues(TEST_INT32, TEST_INT64, u32_val, u64_val);
    auto one_shot_record = db_mgr.findRecord("AllIntegerTypes", one_shot_id);
    EXPECT_EQUAL(one_shot_record->getPropertyInt32("SomeInt32"), TEST_INT32);
    EXPECT_EQUAL(one_shot_record->getPropertyInt64("SomeInt64"), TEST_INT64);
    EXPECT_EQUAL(one_shot_record->getPropertyUInt32("SomeUInt32"), u32_val);
    EXPECT_EQUAL(one_shot_record->getPropertyUInt64("SomeUInt64"), u64_val);

    // Verify createRecordWithColValues() with std::string and const char* (StringTypes).
    auto str_inserter = db_mgr.prepareINSERT(SQL_TABLE("StringTypes"));
    auto str_id_std = str_inserter->createRecordWithColValues(TEST_STRING);
    auto str_rec_std = db_mgr.findRecord("StringTypes", str_id_std);
    EXPECT_EQUAL(str_rec_std->getPropertyString("SomeString"), TEST_STRING);

    auto str_id_cstr = str_inserter->createRecordWithColValues("const char* literal");
    auto str_rec_cstr = db_mgr.findRecord("StringTypes", str_id_cstr);
    EXPECT_EQUAL(str_rec_cstr->getPropertyString("SomeString"), std::string("const char* literal"));

    // Verify createRecordWithColValues() with std::vector (blob) and SqlBlob (BlobTypes).
    auto blob_inserter = db_mgr.prepareINSERT(SQL_TABLE("BlobTypes"));
    auto blob_id_vec = blob_inserter->createRecordWithColValues(TEST_VECTOR);
    auto blob_rec_vec = db_mgr.findRecord("BlobTypes", blob_id_vec);
    EXPECT_EQUAL(blob_rec_vec->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR);

    simdb::SqlBlob blob_arg(TEST_VECTOR2);
    auto blob_id_sql = blob_inserter->createRecordWithColValues(blob_arg);
    auto blob_rec_sql = db_mgr.findRecord("BlobTypes", blob_id_sql);
    EXPECT_EQUAL(blob_rec_sql->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR2);

    // Verify createRecordWithColValues() with mixed types: int, std::string, std::vector (MixAndMatch).
    auto mix_inserter = db_mgr.prepareINSERT(SQL_TABLE("MixAndMatch"));
    const int mix_int = 99;
    const std::string mix_str = "mixed_std::string";
    auto mix_id = mix_inserter->createRecordWithColValues(mix_int, mix_str, TEST_VECTOR);
    auto mix_rec = db_mgr.findRecord("MixAndMatch", mix_id);
    EXPECT_EQUAL(mix_rec->getPropertyInt32("SomeInt32"), mix_int);
    EXPECT_EQUAL(mix_rec->getPropertyString("SomeString"), mix_str);
    EXPECT_EQUAL(mix_rec->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR);

    // Same table with const char* for the string column.
    auto mix_id2 = mix_inserter->createRecordWithColValues(100, "const char* in mix", TEST_VECTOR2);
    auto mix_rec2 = db_mgr.findRecord("MixAndMatch", mix_id2);
    EXPECT_EQUAL(mix_rec2->getPropertyInt32("SomeInt32"), 100);
    EXPECT_EQUAL(mix_rec2->getPropertyString("SomeString"), std::string("const char* in mix"));
    EXPECT_EQUAL(mix_rec2->getPropertyBlob<int>("SomeBlob"), TEST_VECTOR2);

    // Verify that we cannot write to an internal table.
    EXPECT_TRUE(db_mgr.getSchema().hasTable("internal$SchemaTables"));
    EXPECT_THROW(db_mgr.INSERT(SQL_TABLE("internal$SchemaTables")));
    EXPECT_THROW(db_mgr.INSERT(SQL_TABLE("internal$SchemaTables"), SQL_COLUMNS("TableName"), SQL_VALUES("blah")));
    EXPECT_THROW(db_mgr.INSERT(SQL_TABLE("internal$SchemaTables"), SQL_VALUES("x", "y", "z")));
    EXPECT_THROW(db_mgr.prepareINSERT(SQL_TABLE("internal$SchemaTables"), SQL_COLUMNS("TableName")));
    EXPECT_THROW(db_mgr.prepareINSERT(SQL_TABLE("internal$SchemaTables")));
    EXPECT_THROW(db_mgr.EXECUTE("INSERT INTO internal$SchemaTables (TableName) VALUES (\"blah\")"));

    // Negative test: misuse ensureUnique
    simdb::Schema schema3;
    auto& students_tbl = schema3.addTable("Students");
    students_tbl.addColumn("FirstName", dt::string_t);
    students_tbl.addColumn("LastName", dt::string_t);
    students_tbl.addColumn("StudentID", dt::int32_t);
    students_tbl.unsetPrimaryKey();
    students_tbl.ensureUnique("StudentID");

    db_mgr.appendSchema(schema3);
    db_mgr.INSERT(SQL_TABLE("Students"), SQL_VALUES("Bob", "Thompson", 123));
    db_mgr.INSERT(SQL_TABLE("Students"), SQL_VALUES("Alice", "Smith", 456));
    EXPECT_THROW(db_mgr.INSERT(SQL_TABLE("Students"), SQL_VALUES("Chris", "Jackson", 456))); // dup: 456

    // Negative test: misuse INSERT
    EXPECT_THROW(db_mgr.INSERT(
        SQL_TABLE("Students"),
        SQL_COLUMNS("FirstName", "LastName", "StudentID"),
        SQL_VALUES("Jane", "Myers"))); // Values do not match columns

    EXPECT_THROW(db_mgr.INSERT(
        SQL_TABLE("Students"),
        SQL_COLUMNS("FirstName", "LastName"),
        SQL_VALUES("Jane", "Myers", 888))); // Values do not match columns

    REPORT_ERROR;
    return ERROR_CODE;
}
