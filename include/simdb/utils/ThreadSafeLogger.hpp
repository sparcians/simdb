#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "simdb/Exceptions.hpp"

namespace simdb {

/*!
 * \class ThreadSafeLogger
 *
 * \brief Thread-safe logger that serializes writes to an ostream or file. Use
 *        protect() to obtain a Guard; stream into the Guard, then on destruction
 *        the line is written under a lock.
 */
class ThreadSafeLogger
{
public:
    /// \brief Construct a logger that writes to stdout.
    /// \param prefix The prefix to write to the beginning of each line.
    ThreadSafeLogger(const std::string& prefix) :
        out_(&std::cout),
        prefix_(prefix)
    {
    }

    virtual ~ThreadSafeLogger() = default;

    /*!
     * \class Guard
     *
     * \brief RAII handle that buffers streamed output and flushes it to the
     *        logger (under lock) on destruction. Use via ThreadSafeLogger::protect().
     */
    class Guard
    {
    public:
        explicit Guard(const ThreadSafeLogger& logger) :
            logger_(logger)
        {
        }

        ~Guard()
        {
            std::lock_guard<std::mutex> lock(logger_.mutex_);
            (*logger_.out_) << logger_.prefix_ << buffer_.str();
            logger_.out_->flush();
        }

        /// \brief Stream a value into this line's buffer.
        template <typename T> Guard& operator<<(T&& value)
        {
            buffer_ << std::forward<T>(value);
            return *this;
        }

        /// \brief Stream an ostream manipulator (e.g. std::endl) into this line's buffer.
        Guard& operator<<(std::ostream& (*manip)(std::ostream&))
        {
            buffer_ << manip;
            return *this;
        }

    private:
        const ThreadSafeLogger& logger_;
        std::ostringstream buffer_;
    };

    /// \brief Return a Guard that buffers one line; when it is destroyed, the line is written under lock.
    Guard protect() const { return Guard(*this); }

protected:
    /// \brief Construct a logger that writes to a file.
    /// \param fstream The file stream to write to.
    ThreadSafeLogger(std::unique_ptr<std::ofstream> fstream) :
        owned_file_(std::move(fstream)),
        out_(owned_file_.get())
    {
        simdb_assert(owned_file_ && owned_file_->is_open());
        simdb_assert(out_);
    }

private:
    mutable std::mutex mutex_;
    mutable std::unique_ptr<std::ofstream> owned_file_; // only used if file logger
    mutable std::ostream* out_;                         // non-owning or owned via unique_ptr
    const std::string prefix_;
};

/*!
 * \class ThreadSafeFileLogger
 *
 * \brief Thread-safe logger that writes to a file.
 */
class ThreadSafeFileLogger : public ThreadSafeLogger
{
public:
    /// \brief Construct a logger that writes to a file.
    /// \param filename The path to the log file.
    ThreadSafeFileLogger(const std::string& filename) :
        ThreadSafeLogger(std::make_unique<std::ofstream>(filename))
    {
    }
};

} // namespace simdb
