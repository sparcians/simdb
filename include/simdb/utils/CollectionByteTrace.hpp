// <CollectionByteTrace.hpp> -*- C++ -*-
//
// Optional instrumentation: log each logical byte range as it is appended to
// collection StreamBuffers (e.g. simdb_collection_bytes.sim). Disabled by
// default (null thread-local tracer); when enabled, the hot path is one null
// pointer check plus a virtual call on the active sink.

#pragma once

#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace simdb::utils {

/// Receives (byte_count, description) for each traced append, in stream order.
class CollectionByteTracer
{
public:
    virtual ~CollectionByteTracer() = default;

    /// \param num_bytes Number of bytes appended immediately after this call returns.
    /// \param description Human-readable label (e.g. "cid", "minifier action").
    virtual void recordWrite(std::size_t num_bytes, std::string_view description) = 0;
};

/// When non-null, \c StreamBuffer::append forwards each write here before mutating the buffer.
inline thread_local CollectionByteTracer* g_collection_byte_tracer = nullptr;

inline CollectionByteTracer* active_collection_byte_tracer() noexcept
{
    return g_collection_byte_tracer;
}

inline void set_active_collection_byte_tracer(CollectionByteTracer* tracer) noexcept
{
    g_collection_byte_tracer = tracer;
}

/// Writes lines: "<num_bytes>\\t<description>\\n", flushed after each record.
class CollectionByteTraceFileSink final : public CollectionByteTracer
{
public:
    CollectionByteTraceFileSink(const std::string& path, bool reopen_mode)
        : out_(path)
        , path_(reopen_mode ? path : "")
    {
        out_ << "Bytes\tDescription\n";
        out_.flush();
    }

    bool good() const { return out_.good(); }

    void recordWrite(std::size_t num_bytes, std::string_view description) override
    {
        out_ << num_bytes << '\t';
        if (description.empty())
        {
            out_ << "bytes";
        }
        else
        {
            out_ << description;
        }
        out_ << '\n';
        out_.flush();

        if (!path_.empty())
        {
            out_.close();
            out_.open(path_, std::ios::app);
        }
    }

private:
    std::ofstream out_;
    std::string path_;
};

/// Installs a file sink as the thread-local tracer for this scope.
class CollectionByteTraceSession
{
public:
    CollectionByteTraceSession(const std::string& path, bool reopen_mode)
        : prev_(g_collection_byte_tracer)
        , filepath_(path)
    {
        sink_ = std::make_unique<CollectionByteTraceFileSink>(path, reopen_mode);
        g_collection_byte_tracer = sink_.get();
    }

    CollectionByteTraceSession(const CollectionByteTraceSession&) = delete;
    CollectionByteTraceSession& operator=(const CollectionByteTraceSession&) = delete;

    ~CollectionByteTraceSession()
    {
        g_collection_byte_tracer = prev_;
    }

    const std::string& getTraceFile() const { return filepath_; }

    bool sinkGood() const { return sink_ && sink_->good(); }

private:
    CollectionByteTracer* prev_ = nullptr;
    std::unique_ptr<CollectionByteTraceFileSink> sink_;
    std::string filepath_;
};

} // namespace simdb::utils
