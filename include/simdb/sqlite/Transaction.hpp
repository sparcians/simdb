// <Transaction.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/Exceptions.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include <thread>

namespace simdb {

/// To support SimDB self-profiling, return TRUE only if the transaction
/// involved touching the database (setProperty*(), INSERT, SELECT, etc.)
using TransactionFunc = std::function<void()>;

/*!
 * \class SQLiteReturnCode
 * \brief This class wraps a return code and throws a
 * SafeTransactionSilentException when it encounters a "SQL locked" return code.
 */
class SQLiteReturnCode
{
public:
    explicit SQLiteReturnCode(const int rc) :
        rc_(rc)
    {
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED || rc == SQLITE_READONLY)
        {
            throw SafeTransactionSilentException(rc);
        }
    }

    operator int() const { return rc_; }

    operator bool() const { return rc_ != SQLITE_OK; }

    bool operator==(const int rc) { return rc_ == rc; }

    bool operator!=(const int rc) { return rc_ != rc; }

private:
    const int rc_;
};

inline bool operator==(const int rc, const SQLiteReturnCode& obj)
{
    return static_cast<int>(obj) == rc;
}

inline std::ostream& operator<<(std::ostream& os, const SQLiteReturnCode& rc)
{
    os << (int)rc;
    return os;
}

/*!
 * \class SQLitePreparedStatement
 * \brief This class wraps a sqlite3_stmt* and uses RAII to ensure that
 *        sqlite3_finalize() is called so we don't leak resources.
 */
class SQLitePreparedStatement
{
public:
    SQLitePreparedStatement(sqlite3* db_conn, const std::string_view cmd)
    {
        sqlite3_stmt* stmt = nullptr;
        auto rc = sqlite3_prepare_v2(db_conn, cmd.data(), -1, &stmt, 0);
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED || rc == SQLITE_READONLY)
        {
            sqlite3_finalize(stmt);
            throw SafeTransactionSilentException(rc);
        } else
        {
            simdb_assert(stmt, "Invalid prepared statement for cmd: " << cmd);
        }

        stmt_ = stmt;
    }

    SQLitePreparedStatement(sqlite3_stmt* stmt) :
        stmt_(stmt)
    {
    }

    SQLitePreparedStatement(SQLitePreparedStatement&& other) :
        stmt_(other.stmt_)
    {
        other.stmt_ = nullptr;
    }

    SQLitePreparedStatement(const SQLitePreparedStatement& other) = delete;

    ~SQLitePreparedStatement()
    {
        if (stmt_)
        {
            sqlite3_finalize(stmt_);
        }
    }

    operator sqlite3_stmt*() const { return stmt_; }

    sqlite3_stmt* release()
    {
        auto stmt = stmt_;
        stmt_ = nullptr;
        return stmt;
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

/*!
 * \class Transaction
 *
 * \brief Base class for Connection. Made into a base class
 *        to make it easier for SimDB to be a header-only library
 *        that avoids cyclic header includes.
 */
class Transaction
{
public:
    /// Destructor
    virtual ~Transaction() = default;

    /// Execute the functor inside BEGIN/COMMIT TRANSACTION.
    void safeTransaction(const TransactionFunc& transaction)
    {
        while (true)
        {
            try
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);

                // Check to see if we are already in a transaction, in which
                // case we simply call the transaction function. We cannot
                // call "BEGIN TRANSACTION" recursively.
                if (in_transaction_flag_)
                {
                    transaction();
                } else
                {
                    ScopedTransaction scoped_transaction(db_conn_, transaction, in_transaction_flag_);
                    (void)scoped_transaction;
                }

                // We got this far without an exception, which means
                // that the transaction is committed.
                break;
            } catch (const SafeTransactionSilentException&)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
    }

    /// For debug purposes only.
    bool isInTransaction() const { return in_transaction_flag_; }

protected:
    /// Underlying database connection
    sqlite3* db_conn_ = nullptr;

private:
    /// \brief Flag used in RAII safeTransaction() calls. This is
    ///        needed to we know whether to tell SQL to "BEGIN
    ///        TRANSACTION" or not (i.e. if we're already in the
    ///        middle of another safeTransaction).
    ///
    /// This allows users to freely do something like this:
    ///
    /// \code
    ///     db_mgr_->safeTransaction([&]() {
    ///         doFoo();
    ///         doBar();
    ///     });
    /// \endcode
    ///
    /// Even if doFoo() and doBar() do the same thing:
    ///
    /// \code
    ///     void MyClass::doFoo() {
    ///         db_mgr_->safeTransaction([&](){
    ///             ...
    ///         });
    ///     }
    ///
    ///     void MyClass::doBar() {
    ///         db_mgr_->safeTransaction([&](){
    ///             ...
    ///         });
    ///     }
    /// \endcode
    bool in_transaction_flag_ = false;

    /// Mutex for thread-safe reentrant safeTransaction's.
    std::recursive_mutex mutex_;

    /// RAII used for BEGIN/COMMIT TRANSACTION calls. Ensures that
    /// these calls always occur in pairs.
    struct ScopedTransaction
    {
        /// Issues BEGIN TRANSACTION
        ScopedTransaction(sqlite3* db_conn, const TransactionFunc& transaction, bool& in_transaction_flag) :
            db_conn_(db_conn),
            in_transaction_flag_(in_transaction_flag),
            transaction_(transaction)
        {
            in_transaction_flag_ = true;
            executeCommand_("BEGIN TRANSACTION");
            transaction_();
        }

        /// Issues COMMIT TRANSACTION
        ~ScopedTransaction()
        {
            executeCommand_("COMMIT TRANSACTION");
            in_transaction_flag_ = false;
        }

    private:
        /// Execute the provided statement against the database
        /// connection. This will validate the command, and throw
        /// if this command is disallowed.
        void executeCommand_(const char* cmd)
        {
            auto rc = SQLiteReturnCode(sqlite3_exec(db_conn_, cmd, nullptr, nullptr, nullptr));
            if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED || rc == SQLITE_READONLY)
            {
                throw SafeTransactionSilentException(rc);
            } else
            {
                simdb_assert(!rc, sqlite3_errmsg(db_conn_));
            }
        }

        /// Open database connection
        sqlite3* db_conn_ = nullptr;

        /// Reference to Transaction::in_transaction_flag_
        bool& in_transaction_flag_;

        /// Wraps the user's code in a std::function
        const TransactionFunc& transaction_;
    };
};

} // namespace simdb
