// <CollectionByteTrace.hpp> -*- C++ -*-
//
// Optional instrumentation: log each logical byte range as it is appended to
// collection StreamBuffers (e.g. simdb_collection.trace). Disabled by default
// (null thread-local tracer); when enabled, the hot path is one null pointer
// check plus a virtual call on the active sink.

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

    /// Optional structured trace hooks (default no-op) for richer hierarchical logs.
    virtual void beginRecord(const std::string& time_string)
    {
        (void)time_string;
    }
    virtual void endRecord() {}
    virtual void beginGroup(std::string_view, std::size_t = 0) {}
    virtual void endGroup() {}
    virtual void recordValueWrite(
        std::size_t num_bytes,
        std::string_view description,
        std::string_view value_repr)
    {
        (void)value_repr;
        recordWrite(num_bytes, description);
    }
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
        recordValueWrite(num_bytes, description, {});
    }

    void beginRecord(const std::string& time_string) override
    {
        out_ << "record at time " << time_string << "\n";
        indent_ = 1;
        flush_();
    }

    void endRecord() override
    {
        indent_ = 0;
        flush_();
    }

    void beginGroup(std::string_view label, std::size_t total_bytes = 0) override
    {
        writeIndent_();
        if (total_bytes > 0)
        {
            out_ << total_bytes << " bytes, ";
        }
        if (!label.empty())
        {
            out_ << label;
        }
        if (label.empty() || label.back() != ':')
        {
            out_ << ':';
        }
        out_ << '\n';
        ++indent_;
        flush_();
    }

    void endGroup() override
    {
        if (indent_ > 0)
        {
            --indent_;
        }
        flush_();
    }

    void recordValueWrite(
        std::size_t num_bytes,
        std::string_view description,
        std::string_view value_repr) override
    {
        writeIndent_();
        out_ << num_bytes << ' ' << (num_bytes == 1 ? "byte" : "bytes") << ", ";
        out_ << (description.empty() ? std::string_view{"bytes"} : description);
        if (!value_repr.empty())
        {
            out_ << ", value " << value_repr;
        }
        out_ << '\n';
        flush_();
    }

private:
    void writeIndent_()
    {
        for (std::size_t i = 0; i < indent_; ++i)
        {
            out_ << "  ";
        }
    }

    void flush_()
    {
        out_.flush();
        if (!path_.empty())
        {
            out_.close();
            out_.open(path_, std::ios::app);
        }
    }

    std::ofstream out_;
    std::string path_;
    std::size_t indent_ = 0;
};

class ScopedCollectionTraceRecord
{
public:
    explicit ScopedCollectionTraceRecord(CollectionByteTracer* tracer, const std::string& time_string)
        : tracer_(tracer)
    {
        if (tracer_)
        {
            tracer_->beginRecord(time_string);
        }
    }

    ~ScopedCollectionTraceRecord()
    {
        if (tracer_)
        {
            tracer_->endRecord();
        }
    }

private:
    CollectionByteTracer* tracer_ = nullptr;
};

class ScopedCollectionTraceGroup
{
public:
    ScopedCollectionTraceGroup(CollectionByteTracer* tracer, std::string_view label, std::size_t total_bytes = 0)
        : tracer_(tracer)
    {
        if (tracer_)
        {
            tracer_->beginGroup(label, total_bytes);
        }
    }

    ~ScopedCollectionTraceGroup()
    {
        if (tracer_)
        {
            tracer_->endGroup();
        }
    }

private:
    CollectionByteTracer* tracer_ = nullptr;
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
