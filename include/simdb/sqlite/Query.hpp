// <Query.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/sqlite/Constraints.hpp"
#include "simdb/sqlite/Iterator.hpp"
#include <iomanip>
#include <iostream>
#include <limits>

namespace simdb {

/// Used in query->orderBy("ColA", ASC|DESC)
enum class QueryOrder { ASC, DESC };

/// Stringify QueryOrder enums for SELECT commands
inline std::ostream& operator<<(std::ostream& os, const QueryOrder order)
{
    switch (order)
    {
    case QueryOrder::ASC:
        os << "ASC";
        break;
    case QueryOrder::DESC:
        os << "DESC";
        break;
    }

    return os;
}

/*!
 * \class SqlQuery
 *
 * \brief This class issues SELECT statements with Constraints, and is used
 *        in order to iterate over the result set and automatically write
 *        record values into users' local variables.
 */
class SqlQuery
{
public:
    SqlQuery(const char* table_name, sqlite3* db_conn) :
        table_name_(table_name),
        db_conn_(db_conn)
    {
    }

    /// Query for at most N matching records.
    void setLimit(uint32_t limit) { limit_ = limit; }

    /// Remove the LIMIT clause.
    void resetLimit() { limit_ = 0; }

    /// Order the query result set by the given column.
    ///
    ///     // SELECT ... ORDER BY foo DESC
    ///     query->orderBy("foo", QueryOrder::DESC);
    ///
    ///     // SELECT ... ORDER BY foo DESC, bar ASC
    ///     query->orderBy("bar", QueryOrder::ASC);
    ///
    ///     // Remove the ORDER BY clauses
    ///     query->resetOrderBy();
    ///
    ///     // SELECT ... ORDER BY bar ASC
    ///     query->orderBy("bar", QueryOrder::ASC);
    void orderBy(const char* col_name, const QueryOrder order) { order_clauses_.emplace_back(col_name, order); }

    /// Remove the ORDER BY clauses.
    void resetOrderBy() { order_clauses_.clear(); }

    /// Add a GROUP BY clause. Only one allowed.
    void groupBy(const char* col_name)
    {
        simdb_assert(group_by_column_.empty(), "Only one GROUP BY clause allowed");
        group_by_column_ = col_name;
    }

    /// Remove the GROUP BY clause.
    void resetGroupBy() { group_by_column_.clear(); }

    /// Add a constraint to this query specific to integer types.
    template <typename T> void addConstraintForInt(const char* col_name, const Constraints constraint, const T target)
    {
        static_assert(!std::is_same<T, uint64_t>::value, "Wrong addConstraint*() API");

        std::ostringstream oss;
        oss << col_name << stringify(constraint) << target;
        constraint_clauses_.emplace_back(oss.str());
    }

    /// Same as addConstraintForInt() but for users who want explicit APIs.
    void addConstraintForInt32(const char* col_name, const Constraints constraint, const int32_t target)
    {
        addConstraintForInt<int32_t>(col_name, constraint, target);
    }

    /// Same as addConstraintForInt() but for users who want explicit APIs.
    void addConstraintForUInt32(const char* col_name, const Constraints constraint, const uint32_t target)
    {
        addConstraintForInt<uint32_t>(col_name, constraint, target);
    }

    /// Same as addConstraintForInt() but for users who want explicit APIs.
    void addConstraintForInt64(const char* col_name, const Constraints constraint, const int64_t target)
    {
        addConstraintForInt<int64_t>(col_name, constraint, target);
    }

    /// Add a uint64_t constraint to this query.
    void addConstraintForUInt64(const char* col_name, const Constraints constraint, uint64_t target)
    {
        char digits[21];
        for (int i = 19; i >= 0; --i)
        {
            digits[i] = '0' + (target % 10);
            target /= 10;
        }
        digits[20] = '\0';

        std::string clause;
        auto constraint_str = stringify(constraint);

        // column + op + digits + quotes
        clause.reserve(strlen(col_name) + constraint_str.size() + 20 + 2);

        clause += col_name;
        clause += constraint_str;
        clause += '"';
        clause += digits;
        clause += '"';

        constraint_clauses_.emplace_back(std::move(clause));
    }

    /// Add a constraint to this query specific to floating-point types.
    /// Pass in fuzzy=TRUE to tell SQLite to look for matches that are
    /// within EPS of the target value.
    template <typename T>
    void addConstraintForDouble(const char* col_name, const Constraints constraint, const T target, bool fuzzy = false)
    {
        static_assert(std::is_floating_point<T>::value, "Wrong addConstraint*() API");

        std::ostringstream oss;
        if (fuzzy)
        {
            oss << "fuzzyMatch(" << col_name << ",";
            oss << std::setprecision(std::numeric_limits<T>::max_digits10);
            oss << target << ",";
            oss << static_cast<int>(constraint) << ")";
        } else
        {
            oss << col_name << stringify(constraint);
            oss << std::setprecision(std::numeric_limits<T>::max_digits10) << target;
        }

        constraint_clauses_.emplace_back(oss.str());
    }

    /// Add a constraint to this query specific to string types.
    void addConstraintForString(const char* col_name, const Constraints constraint, const std::string& target)
    {
        addConstraintForString(col_name, constraint, target.c_str());
    }

    /// Add a constraint to this query specific to string types.
    void addConstraintForString(const char* col_name, const Constraints constraint, const char* target)
    {
        std::ostringstream oss;
        oss << col_name << stringify(constraint) << "'" << target << "'";
        constraint_clauses_.emplace_back(oss.str());
    }

    /// Add a constraint to this query specific to integer types (except
    /// uint64_t) and multiple target values.
    template <typename T>
    void addConstraintForInt(const char* col_name, const SetConstraints constraint,
                             const std::initializer_list<T>& targets)
    {
        addConstraintForInt(col_name, constraint, std::vector<T>{targets.begin(), targets.end()});
    }

    /// Add a constraint to this query specific to integer types (except
    /// uint64_t) and multiple target values.
    template <typename T>
    void addConstraintForInt(const char* col_name, const SetConstraints constraint, const std::vector<T>& targets)
    {
        static_assert(!std::is_same<T, uint64_t>::value, "Wrong addConstraint*() API");

        std::ostringstream oss;
        oss << col_name << stringify(constraint) << "(";

        for (size_t idx = 0; idx < targets.size(); ++idx)
        {
            oss << targets[idx];
            if (idx != targets.size() - 1)
            {
                oss << ",";
            }
        }

        oss << ")";
        constraint_clauses_.emplace_back(oss.str());
    }

    /// Add a constraint to this query for uint64_t types and multiple target
    /// values.
    void addConstraintForUInt64(const char* col_name, const SetConstraints constraint,
                                const std::initializer_list<uint64_t>& targets)
    {
        addConstraintForUInt64(col_name, constraint, std::vector<uint64_t>{targets.begin(), targets.end()});
    }

    /// Add a constraint to this query for uint64_t types and multiple target
    /// values.
    void addConstraintForUInt64(const char* col_name, const SetConstraints constraint,
                                const std::vector<uint64_t>& targets)
    {
        std::string clause;
        auto constraint_str = stringify(constraint);

        // column + op + digits + quotes + commas + parens
        clause.reserve(strlen(col_name) + constraint_str.size() + 20 + 2 + (targets.size() - 1) + 2);
        clause += col_name;
        clause += constraint_str;
        clause += "(";

        for (size_t i = 0; i < targets.size(); ++i)
        {
            auto target = targets[i];
            char digits[21];
            for (int i = 19; i >= 0; --i)
            {
                digits[i] = '0' + (target % 10);
                target /= 10;
            }
            digits[20] = '\0';
            clause += '"';
            clause += digits;
            clause += '"';

            if (i != targets.size() - 1)
            {
                clause += ",";
            }
        }
        clause += ")";

        constraint_clauses_.emplace_back(std::move(clause));
    }

    /// Add a constraint to this query specific to floating-point types
    /// and multiple target values.
    ///
    /// Pass in fuzzy=TRUE to tell SQLite to look for matches that are
    /// within EPS of the target values.
    template <typename T>
    void addConstraintForDouble(const char* col_name, const SetConstraints constraint,
                                const std::initializer_list<T>& targets, bool fuzzy = false)
    {
        addConstraintForDouble(col_name, constraint, std::vector<T>{targets.begin(), targets.end()}, fuzzy);
    }

    /// Add a constraint to this query specific to floating-point types
    /// and multiple target values.
    ///
    /// Pass in fuzzy=TRUE to tell SQLite to look for matches that are
    /// within EPS of the target values.
    template <typename T>
    void addConstraintForDouble(const char* col_name, const SetConstraints constraint, const std::vector<T>& targets,
                                bool fuzzy = false)
    {
        static_assert(std::is_floating_point<T>::value, "Wrong addConstraint*() API");

        std::ostringstream oss;
        if (fuzzy)
        {
            oss << "(";

            for (size_t idx = 0; idx < targets.size(); ++idx)
            {
                std::ostringstream target_oss;
                target_oss << "fuzzyMatch(" << col_name << ",";
                target_oss << std::setprecision(std::numeric_limits<T>::max_digits10);
                target_oss << targets[idx] << ",";

                if (constraint == SetConstraints::IN_SET)
                {
                    target_oss << static_cast<int>(Constraints::EQUAL);
                } else
                {
                    target_oss << static_cast<int>(Constraints::NOT_EQUAL);
                }

                target_oss << ")";
                oss << target_oss.str();

                if (idx != targets.size() - 1)
                {
                    if (constraint == SetConstraints::IN_SET)
                    {
                        oss << " OR ";
                    } else
                    {
                        oss << " AND ";
                    }
                }
            }

            oss << ")";
        } else
        {
            oss << col_name << stringify(constraint) << " (";
            for (size_t idx = 0; idx < targets.size(); ++idx)
            {
                oss << std::setprecision(std::numeric_limits<T>::max_digits10) << targets[idx];
                if (idx != targets.size() - 1)
                {
                    oss << ",";
                }
            }
            oss << ")";
        }

        constraint_clauses_.emplace_back(oss.str());
    }

    /// Add a constraint to this query specific to string types and
    /// multiple target values.
    void addConstraintForString(const char* col_name, const SetConstraints constraint,
                                const std::initializer_list<const char*>& targets)
    {
        addConstraintForString(col_name, constraint, std::vector<std::string>{targets.begin(), targets.end()});
    }

    /// Add a constraint to this query specific to string types and
    /// multiple target values.
    void addConstraintForString(const char* col_name, const SetConstraints constraint,
                                const std::vector<std::string>& targets)
    {
        std::ostringstream oss;
        oss << col_name << stringify(constraint) << "(";

        for (size_t idx = 0; idx < targets.size(); ++idx)
        {
            oss << "'" << targets[idx] << "'";
            if (idx != targets.size() - 1)
            {
                oss << ",";
            }
        }

        oss << ")";
        constraint_clauses_.emplace_back(oss.str());
    }

    /// After one or more calls to addConstraint*(), this function can be called
    /// to create a compound constraint using AND or OR.
    ///
    ///     // SELECT ... (WHERE ColA = 1 AND ColB = 2) OR (ColC = 3)
    ///     query->addConstraintForInt("ColA", Constraints::EQUAL, 1);
    ///     query->addConstraintForInt("ColB", Constraints::EQUAL, 2);
    ///     auto clause1 = query->releaseConstraintClauses();
    ///
    ///     query->addConstraintForInt("ColC", Constraints::EQUAL, 3);
    ///     auto clause2 = query->releaseConstraintClauses();
    ///
    ///     query->addCompoundConstraint(clause1, QueryOperator::OR, clause2);
    void addCompoundConstraint(const std::vector<std::string>& clause1, const QueryOperator compound_constraint,
                               const std::vector<std::string>& clause2)
    {
        std::ostringstream oss;
        oss << "(";
        for (size_t idx = 0; idx < clause1.size(); ++idx)
        {
            oss << clause1[idx];
            if (idx != clause1.size() - 1)
            {
                oss << " AND ";
            }
        }

        if (compound_constraint == QueryOperator::AND)
        {
            oss << ") AND (";
        } else
        {
            oss << ") OR (";
        }

        for (size_t idx = 0; idx < clause2.size(); ++idx)
        {
            oss << clause2[idx];
            if (idx != clause2.size() - 1)
            {
                oss << " AND ";
            }
        }

        oss << ")";
        constraint_clauses_.emplace_back(oss.str());
    }

    /// Release the current constraint clauses.
    std::vector<std::string> releaseConstraintClauses() { return std::move(constraint_clauses_); }

    /// Reset the query constraints.
    void resetConstraints() { constraint_clauses_.clear(); }

    /// Non-optional scalar SELECT overloads throw if a column value is SQL NULL.
    /// Use the std::optional overloads when a column may be unset.
    ///
    /// SELECT column values and write to the local variable on each iteration
    /// (int32_t).
    ///
    ///     int32_t val;
    ///     query->select("Col", val);
    void select(const char* col_name, int32_t& user_var)
    {
        result_writers_.emplace_back(new ResultWriterInt32(col_name, &user_var));
    }

    /// Use std::optional overload to query record values that may be NULL/unset
    void select(const char* col_name, std::optional<int32_t>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterInt32<std::optional<int32_t>>(col_name, &user_var));
    }

    /// SELECT column values and write to the local variable on each iteration
    /// (uint32_t).
    void select(const char* col_name, uint32_t& user_var)
    {
        result_writers_.emplace_back(new ResultWriterUInt32(col_name, &user_var));
    }

    /// Use std::optional overload to query record values that may be NULL/unset
    void select(const char* col_name, std::optional<uint32_t>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterUInt32<std::optional<uint32_t>>(col_name, &user_var));
    }

    /// SELECT column values and write to the local variable on each iteration
    /// (int64_t).
    ///
    ///     int64_t val;
    ///     query->select("Col", val);
    void select(const char* col_name, int64_t& user_var)
    {
        result_writers_.emplace_back(new ResultWriterInt64(col_name, &user_var));
    }

    /// Use std::optional overload to query record values that may be NULL/unset
    void select(const char* col_name, std::optional<int64_t>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterInt64<std::optional<int64_t>>(col_name, &user_var));
    }

    /// SELECT column values and write to the local variable on each iteration
    /// (uint64_t).
    ///
    ///     uint64_t val;
    ///     query->select("Col", val);
    void select(const char* col_name, uint64_t& user_var)
    {
        result_writers_.emplace_back(new ResultWriterUInt64(col_name, &user_var));
    }

    /// Use std::optional overload to query record values that may be NULL/unset
    void select(const char* col_name, std::optional<uint64_t>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterUInt64<std::optional<uint64_t>>(col_name, &user_var));
    }

    /// SELECT column values and write to the local variable on each iteration
    /// (double).
    ///
    ///     double val;
    ///     query->select("Col", val);
    void select(const char* col_name, double& user_var)
    {
        result_writers_.emplace_back(new ResultWriterDouble(col_name, &user_var));
    }

    /// Use std::optional overload to query record values that may be NULL/unset
    void select(const char* col_name, std::optional<double>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterDouble<std::optional<double>>(col_name, &user_var));
    }

    /// SELECT column values and write to the local variable on each iteration
    /// (string).
    ///
    ///     std::string val;
    ///     query->select("Col", val);
    ///
    /// NULL columns are written as the empty string. Use the std::optional overload
    /// to distinguish SQL NULL from an explicitly stored empty string.
    void select(const char* col_name, std::string& user_var)
    {
        result_writers_.emplace_back(new ResultWriterString(col_name, &user_var));
    }

    /// Use std::optional overload to distinguish SQL NULL from "".
    void select(const char* col_name, std::optional<std::string>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterString<std::optional<std::string>>(col_name, &user_var));
    }

    /// SELECT column values and write to the local variable on each iteration
    /// (blob).
    ///
    ///     std::vector<int> val;
    ///     query->select("Col", val);
    ///
    /// Note that this does not have a std::optional overload since std::vector::empty()
    /// makes NULL obvious.
    template <typename T> void select(const char* col_name, std::vector<T>& user_var)
    {
        result_writers_.emplace_back(new ResultWriterBlob<T>(col_name, &user_var));
    }

    /// Deselect all record property values.
    void resetSelections() { result_writers_.clear(); }

    /// Count the number of records matching this query's search constraints.
    /// If no constraints were added, counts all records in the table.
    uint64_t count()
    {
        std::ostringstream oss;
        oss << "SELECT COUNT(*) FROM " << table_name_ << " ";
        appendConstraintClauses_(oss);

        const auto cmd = oss.str();
        auto stmt = SQLitePreparedStatement(db_conn_, cmd);
        auto rc = SQLiteReturnCode(sqlite3_step(stmt));

        if (rc == SQLITE_ROW)
        {
            return sqlite3_column_int64(stmt, 0);
        }

        if (rc == SQLITE_DONE)
        {
            return 0;
        }

        throw DBException(sqlite3_errmsg(db_conn_));
    }

    /// DELETE all records that match the query's current search constraints.
    void deleteResultSet()
    {
        std::ostringstream oss;
        oss << "DELETE FROM " << table_name_ << " ";
        appendConstraintClauses_(oss);

        const auto cmd = oss.str();
        auto stmt = SQLitePreparedStatement(db_conn_, cmd);
        simdb_assert(sqlite3_step(stmt) == SQLITE_DONE, sqlite3_errmsg(db_conn_));
    }

    /// Execute the query. Returns an iterator to walk the result set
    /// row-by-row and populate the user's local variables given to
    /// the select() calls.
    SqlResultIterator getResultSet()
    {
        std::ostringstream oss;
        oss << "SELECT ";
        for (size_t idx = 0; idx < result_writers_.size(); ++idx)
        {
            oss << result_writers_[idx]->getColName();
            if (idx != result_writers_.size() - 1)
            {
                oss << ",";
            }
        }

        oss << " FROM " << table_name_ << " ";

        appendConstraintClauses_(oss);
        appendGroupByClause_(oss);
        appendOrderByClauses_(oss);
        appendLimitClause_(oss);

        const auto cmd = oss.str();
        auto stmt = SQLitePreparedStatement(db_conn_, cmd);

        std::vector<std::shared_ptr<ResultWriterBase>> result_writers;
        for (const auto& writer : result_writers_)
        {
            result_writers.emplace_back(writer->clone());
        }

        return SqlResultIterator(stmt.release(), std::move(result_writers));
    }

private:
    /// Append WHERE clause(s).
    void appendConstraintClauses_(std::ostringstream& oss) const
    {
        if (!constraint_clauses_.empty())
        {
            oss << " WHERE ";
            for (size_t idx = 0; idx < constraint_clauses_.size(); ++idx)
            {
                oss << constraint_clauses_[idx] << " ";
                if (idx != constraint_clauses_.size() - 1)
                {
                    oss << " AND ";
                }
            }
            oss << " ";
        }
    }

    /// Append GROUP BY clause.
    void appendGroupByClause_(std::ostringstream& oss) const
    {
        if (!group_by_column_.empty())
        {
            oss << " GROUP BY " << group_by_column_ << " ";
        }
    }

    /// Append ORDER BY clause(s).
    void appendOrderByClauses_(std::ostringstream& oss) const
    {
        if (!order_clauses_.empty())
        {
            oss << " ORDER BY ";
            for (size_t idx = 0; idx < order_clauses_.size(); ++idx)
            {
                oss << order_clauses_[idx].col_name << " " << order_clauses_[idx].order;
                if (idx != order_clauses_.size() - 1)
                {
                    oss << ",";
                }
            }
            oss << " ";
        }
    }

    /// Append LIMIT clause.
    void appendLimitClause_(std::ostringstream& oss) const
    {
        if (limit_)
        {
            oss << " LIMIT " << limit_;
        }
    }

    /// \struct QueryOrderClause
    /// \brief  Used to build clauses like "ORDER BY ColA ASC, ColB DESC"
    struct QueryOrderClause
    {
        std::string col_name;
        QueryOrder order;

        QueryOrderClause(const char* col_name, const QueryOrder order) :
            col_name(col_name),
            order(order)
        {
        }
    };

    /// SELECT ColA,ColB FROM <table_name_> WHERE ...
    const std::string table_name_;

    /// Underlying sqlite3 database
    sqlite3* const db_conn_;

    /// SELECT ColA,ColB FROM Table WHERE ... LIMIT <limit_>
    uint32_t limit_ = 0;

    /// SELECT ColA,ColB FROM Table WHERE ... ORDER BY <order_clauses_>
    std::vector<QueryOrderClause> order_clauses_;

    /// SELECT ColA,ColB FROM Table WHERE ... GROUP BY <group_by_column_>
    std::string group_by_column_;

    /// SELECT ColA,ColB FROM Table WHERE <constraint_clauses_>
    std::vector<std::string> constraint_clauses_;

    /// SELECT <result_writers_> FROM Table WHERE ...
    std::vector<std::shared_ptr<ResultWriterBase>> result_writers_;
};

} // namespace simdb
