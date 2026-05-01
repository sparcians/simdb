// <CollectionByteTrace.hpp> -*- C++ -*-
//
// Optional instrumentation: log each logical byte range as it is appended to
// collection StreamBuffers (e.g. simdb_collection.trace). Disabled by default
// (null thread-local tracer); when enabled, the hot path is one null pointer
// check plus a virtual call on the active sink.

#pragma once

#include <cstddef>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
    virtual void flushThrough(std::string_view) {}
    virtual void flushAllPending() {}
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
    }

    bool good() const { return out_.good(); }

    void recordWrite(std::size_t num_bytes, std::string_view description) override
    {
        recordValueWrite(num_bytes, description, {});
    }

    void beginRecord(const std::string& time_string) override
    {
        current_time_key_ = parseTimeKey_(time_string);
        current_record_.str({});
        current_record_.clear();
        current_record_ << "record at time " << time_string << "\n";
        indent_ = 1;
    }

    void endRecord() override
    {
        indent_ = 0;
        pending_records_[current_time_key_].emplace_back(current_record_.str());
        current_record_.str({});
        current_record_.clear();
    }

    void beginGroup(std::string_view label, std::size_t total_bytes = 0) override
    {
        writeIndent_();
        if (total_bytes > 0)
        {
            current_record_ << total_bytes << " bytes, ";
        }
        if (!label.empty())
        {
            current_record_ << label;
        }
        if (label.empty() || label.back() != ':')
        {
            current_record_ << ':';
        }
        current_record_ << '\n';
        ++indent_;
    }

    void endGroup() override
    {
        if (indent_ > 0)
        {
            --indent_;
        }
    }

    void recordValueWrite(
        std::size_t num_bytes,
        std::string_view description,
        std::string_view value_repr) override
    {
        writeIndent_();
        current_record_ << num_bytes << ' ' << (num_bytes == 1 ? "byte" : "bytes") << ", ";
        current_record_ << (description.empty() ? std::string_view{"bytes"} : description);
        if (!value_repr.empty())
        {
            current_record_ << ", value " << value_repr;
        }
        current_record_ << '\n';
    }

    void flushThrough(std::string_view time_string) override
    {
        const auto max_key = parseTimeKey_(time_string);
        auto it = pending_records_.begin();
        while (it != pending_records_.end() && it->first <= max_key)
        {
            for (const auto& record_text : it->second)
            {
                out_ << record_text;
            }
            it = pending_records_.erase(it);
        }
        flush_();
    }

    void flushAllPending() override
    {
        flushThrough(std::to_string(std::numeric_limits<uint64_t>::max()));
    }

private:
    void writeIndent_()
    {
        for (std::size_t i = 0; i < indent_; ++i)
        {
            current_record_ << "  ";
        }
    }

    static uint64_t parseTimeKey_(std::string_view time_string)
    {
        try
        {
            return static_cast<uint64_t>(std::stoull(std::string(time_string)));
        }
        catch (...)
        {
            // Fallback for non-integer time labels in debug traces.
            return std::numeric_limits<uint64_t>::max();
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
    std::ostringstream current_record_;
    uint64_t current_time_key_ = 0;
    std::map<uint64_t, std::vector<std::string>> pending_records_;
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
        if (sink_)
        {
            sink_->flushAllPending();
        }
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
