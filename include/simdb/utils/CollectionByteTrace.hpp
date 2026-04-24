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
    explicit CollectionByteTraceFileSink(const std::string& path) : out_(path, std::ios::binary | std::ios::trunc)
    {
    }

    bool good() const { return out_.good(); }

    void recordWrite(std::size_t num_bytes, std::string_view description) override
    {
        out_ << num_bytes << '\t' << description << '\n';
        out_.flush();
    }

private:
    std::ofstream out_;
};

/// Installs a file sink as the thread-local tracer for this scope.
class CollectionByteTraceSession
{
public:
    CollectionByteTraceSession(const std::string& path)
        : prev_(g_collection_byte_tracer)
        , filepath_(path)
    {
        sink_ = std::make_unique<CollectionByteTraceFileSink>(path);
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
