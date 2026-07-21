// <DatabaseAccessor.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/pipeline/AsyncDatabaseAccessor.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/sqlite/PreparedINSERT.hpp"
#include "simdb/sqlite/Table.hpp"

namespace simdb::pipeline {

/*!
 * \class DatabaseAccessor
 *
 * \brief Provides DatabaseManager access and per-App prepared INSERT objects
 *        for DatabaseStage. Used by DatabaseStage<AppT> to get the manager
 *        and getTableInserter<AppT>(table_name) for high-volume inserts.
 */
class DatabaseAccessor
{
public:
    /// \brief Construct with the DatabaseManager used for all operations.
    /// \param db_mgr Non-null DatabaseManager.
    DatabaseAccessor(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
    }

    /// \brief Return the DatabaseManager.
    DatabaseManager* getDatabaseManager() const { return db_mgr_; }

    /// \brief Return a prepared INSERT for the given table; lazily builds inserters from App::defineSchema().
    /// \tparam App App type (must have NAME and defineSchema()).
    /// \param tbl_name Table name in the App's schema.
    /// \return Raw pointer to the PreparedINSERT (owned by this accessor).
    /// \throws DBException if the table is not in the App's schema.
    template <typename App> PreparedINSERT* getTableInserter(const std::string& tbl_name)
    {
        auto& inserters = tbl_inserters_by_app_[App::NAME];
        if (inserters.empty())
        {
            Schema schema;
            App::defineSchema(schema);

            for (const auto& tbl : schema.getTables())
            {
                std::vector<std::string> col_names;
                for (const auto& col : tbl.getColumns())
                {
                    col_names.emplace_back(col->getName());
                }

                SqlTable table(tbl.getName());
                SqlColumns columns(col_names);
                auto inserter = db_mgr_->prepareINSERT(std::move(table), std::move(columns));
                inserters[tbl.getName()] = std::move(inserter);
            }
        }

        auto& inserter = inserters[tbl_name];
        simdb_assert(inserter, "Table does not exist in schema for app: " << App::NAME);

        return inserter.get();
    }

private:
    DatabaseManager* db_mgr_ = nullptr;

    using TableInserters = std::unordered_map<std::string, std::unique_ptr<PreparedINSERT>>;
    std::unordered_map<std::string, TableInserters> tbl_inserters_by_app_;
};

} // namespace simdb::pipeline
