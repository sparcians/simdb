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
            std::unique_lock<std::recursive_mutex> lock(mutex_);

            // Check to see if we are already in a transaction, in which
            // case we simply call the transaction function. We cannot
            // call "BEGIN TRANSACTION" recursively.
            //
            // Note that any exception thrown here (including a
            // SafeTransactionSilentException) must propagate up to the
            // outermost safeTransaction() call below -- that call is the
            // one that issued "BEGIN TRANSACTION" and is the only one
            // that should roll back and/or retry. Retrying or rolling
            // back here would act on only part of the outer transaction.
            if (in_transaction_flag_)
            {
                transaction();
                return;
            }

            // We are the outermost safeTransaction() call. Set the flag
            // for the duration of BEGIN/COMMIT/ROLLBACK so that any nested
            // safeTransaction() calls (including ones made indirectly by
            // the exception paths below) know not to issue their own
            // BEGIN TRANSACTION. The flag is unconditionally cleared
            // before this call returns or throws, on every code path.
            in_transaction_flag_ = true;
            try
            {
                executeCommand_("BEGIN TRANSACTION");
                transaction();
                executeCommand_("COMMIT TRANSACTION");
                in_transaction_flag_ = false;
                return;
            } catch (const SafeTransactionSilentException&)
            {
                in_transaction_flag_ = false;
                rollbackSilently_();
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                // Loop around and retry the whole transaction from scratch.
            } catch (...)
            {
                in_transaction_flag_ = false;
                rollbackSilently_();
                throw;
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

    /// Execute the provided statement against the database connection.
    /// This will validate the command, and throw if this command is
    /// disallowed. Used for "BEGIN TRANSACTION" and "COMMIT TRANSACTION"
    /// -- both of these are run as regular (non-destructor) code so that
    /// any exception they throw, including SafeTransactionSilentException,
    /// is always caught by safeTransaction()'s try/catch instead of
    /// escaping from a noexcept destructor and calling std::terminate().
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

    /// Best-effort "ROLLBACK TRANSACTION", used to undo a transaction that
    /// failed partway through (whether from a SafeTransactionSilentException
    /// that we are about to retry, or any other exception propagating out of
    /// safeTransaction()). We are already unwinding/handling another error
    /// here, so failures from the rollback itself are intentionally ignored
    /// rather than thrown -- throwing here could mask the original exception
    /// or (if BEGIN TRANSACTION never actually succeeded) fail spuriously.
    void rollbackSilently_() noexcept { sqlite3_exec(db_conn_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr); }
};

} // namespace simdb
