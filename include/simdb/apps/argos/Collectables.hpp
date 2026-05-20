// <Collectables.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

/// Base class for all collectables.
class CollectableBase
{
public:
    virtual ~CollectableBase() = default;

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// \brief Connect to the ArgosCollector's main input queue
    void connectToPipeline(PipelineStagerBase* stager) { stager_ = stager; }

    /// Enable collection
    void enable()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (!enabled_)
        {
            // TODO cnyce: handle initial value on first enable()
            enabled_ = true;
            stager_->onEnabledChanged(getID(), enabled_);
        }
    }

    /// Disable collection
    void disable()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (enabled_)
        {
            enabled_ = false;
            stager_->onEnabledChanged(getID(), enabled_);
        }
    }

    /// Suppress heartbeat re-emission of previously seen bytes.
    void quiet()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (!quiet_)
        {
            quiet_ = true;
            stager_->onQuietChanged(getID(), quiet_);
        }
    }

    /// Re-enable heartbeat re-emission of previously seen bytes.
    void awaken()
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }

        if (quiet_)
        {
            quiet_ = false;
            stager_->onQuietChanged(getID(), quiet_);
        }
    }

    /// Check enabled
    bool enabled() const { return enabled_ && stager_ != nullptr; }

    /// Check whether heartbeat re-emission is suppressed.
    bool quieted() const { return quiet_; }

    /// Demangled element type for scalars, or element demangle + \c _contig_capacityN /
    /// \c _sparse_capacityN for queues.
    virtual std::string collectableTypeNameForDb() const = 0;

    /// Write any final metadata during the ArgosCollector::postTeardown() phase.
    virtual void writeMetaOnPostTeardown(DatabaseManager*) const {}

    /// For testing purposes only. DO NOT CALL IN PRODUCTION.
    static void resetCIDs() { nextCID_() = 0; }

protected:
    explicit CollectableBase(size_t heartbeat) :
        heartbeat_(heartbeat)
    {
    }

    /// Get the heartbeat value for all collection points.
    size_t getHeartbeat_() const { return heartbeat_; }

    template <typename T> static std::string scalarTypeName_()
    {
        if constexpr (std::is_same_v<T, std::string>)
        {
            return "string";
        } else
        {
            return simdb::demangle_type<T>();
        }
    }

    /// Stage collected bytes for pipeline processing.
    void stage_(CollectedData&& data)
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }
        stager_->stage(std::move(data));
    }

private:
    /// Unique ID generator.
    static uint16_t& nextCID_()
    {
        static uint16_t counter = 0;
        if (counter == UINT16_MAX)
        {
            throw DBException("Max number of collectables exceeded (") << UINT16_MAX << ")";
        }
        ++counter;
        return counter;
    }

    /// Unique collectable ID
    const uint16_t cid_{nextCID_()};

    /// Heartbeat value for this collection point. This is the
    /// maximum number of cycles SimDB will attempt to perform
    /// "minification" on the data before it is forced to write
    /// the whole un-minified value to the database again. Note
    /// that minification is simply an implementation detail
    /// for performance.
    const size_t heartbeat_;

    /// Enabled flag
    bool enabled_ = true;

    /// Suppress heartbeat re-emission while true.
    bool quiet_ = false;

    /// Main entry point into the pipeline
    PipelineStagerBase* stager_ = nullptr;
};

/// TODO cnyce: This non-template class is only being used here
/// to help get past all the compiler errors related to templates.
/// This is not the design's final form; the ScalarCollector and
/// ContainerCollector classes should be used once the dust settles.
class CollectionEntryPoint : public CollectableBase
{
public:
    CollectionEntryPoint(size_t heartbeat, TinyStrings<>* tiny_strings) :
        CollectableBase(heartbeat),
        tiny_strings_(tiny_strings)
    {
    }

    TinyStrings<>* getTinyStrings() const { return tiny_strings_; }

    void setScalarDataType(const std::string& dtype)
    {
        assert(collectable_type_name_.empty());
        collectable_type_name_ = dtype;
        is_scalar_ = true;
    }

    void setContainerDataType(const std::string& bin_dtype, bool sparse, size_t capacity)
    {
        assert(collectable_type_name_.empty());
        collectable_type_name_ = bin_dtype;
        if (sparse)
        {
            collectable_type_name_ += "_sparse_capacity";
            is_sparse_container_ = true;
        } else
        {
            collectable_type_name_ += "_contig_capacity";
            is_contig_container_ = true;
        }
        collectable_type_name_ += std::to_string(capacity);
    }

    std::string collectableTypeNameForDb() const override final
    {
        if (collectable_type_name_.empty())
        {
            throw DBException("Collectable data type name never set!");
        }
        return collectable_type_name_;
    }

    template <typename ScalarT> void setScalarValue(const ScalarT& val)
    {
        if (!enabled())
        {
            return;
        }

        if constexpr (std::is_enum_v<ScalarT>)
        {
            std::ostringstream oss;
            oss << val;
            auto enum_str = oss.str();
            auto enum_sid = tiny_strings_->getStringID(enum_str);
            setScalarValue(enum_sid);
        } else if constexpr (std::is_same_v<ScalarT, std::string>)
        {
            auto sid = tiny_strings_->getStringID(val);
            setScalarValue(sid);
        } else if constexpr (std::is_same_v<std::decay_t<ScalarT>, const char*>)
        {
            auto sid = tiny_strings_->getStringID(val);
            setScalarValue(sid);
        } else
        {
            static_assert(std::is_trivial_v<ScalarT> && std::is_standard_layout_v<ScalarT>);
            std::vector<char> bytes;
            StreamBuffer buf(bytes);
            buf.append(val);
            setScalarValueBytes(bytes);
        }
    }

    void setScalarValueBytes(const std::vector<char>& bytes)
    {
        if (!enabled())
        {
            return;
        }

        assert(is_scalar_);
        sendBytes_(bytes);
    }

    void setContigContainerBinBytes(const std::vector<std::vector<char>>& bin_bytes)
    {
        if (!enabled())
        {
            return;
        }

        assert(is_contig_container_);
        uint64_t size = 0;
        for (const auto& bytes : bin_bytes)
        {
            if (!bytes.empty())
            {
                ++size;
            } else
            {
                break;
            }
        }

        assert(size <= UINT16_MAX);
        std::vector<char> final_bytes;
        StreamBuffer buf(final_bytes);
        buf.append((uint16_t)size);

        for (uint64_t i = 0; i < size; ++i)
        {
            buf.append(bin_bytes[i]);
        }

        sendBytes_(final_bytes);
    }

    void setSparseContainerBinBytes(const std::map<uint16_t, std::vector<char>>& bin_bytes)
    {
        if (!enabled())
        {
            return;
        }

        assert(is_sparse_container_);
        uint64_t size = 0;
        for (const auto& [_, bytes] : bin_bytes)
        {
            if (!bytes.empty())
            {
                ++size;
            }
        }

        assert(size <= UINT16_MAX);
        std::vector<char> final_bytes;
        StreamBuffer buf(final_bytes);
        buf.append((uint16_t)size);

        for (const auto& [bin_idx, bytes] : bin_bytes)
        {
            if (!bytes.empty())
            {
                buf.append(bin_idx);
                buf.append(bytes);
            }
        }

        sendBytes_(final_bytes);
    }

private:
    void sendBytes_(const std::vector<char>& bytes)
    {
        assert(enabled());

        if (quieted())
        {
            awaken();
        }

        CollectedData collected(getID());
        auto& buf = collected.getBuffer();
        buf.append(FULL_ACTION_FLAG); // TODO cnyce: minifiers
        buf.append(bytes);
        stage_(std::move(collected));
    }

    std::string collectable_type_name_;
    ValidValue<bool> is_scalar_;
    ValidValue<bool> is_contig_container_;
    ValidValue<bool> is_sparse_container_;
    TinyStrings<>* tiny_strings_ = nullptr;
};

#if 0
/// Template class for all scalar types (POD, struct-like, string, enum, bool)
template <typename ScalarT>
class ScalarCollector : public CollectableBase
{
public:
    using ValueType = type_traits::remove_any_pointer_t<ScalarT>;
    static constexpr auto is_string = std::is_same_v<ValueType, std::string>;
    static constexpr auto is_enum = std::is_enum_v<ValueType>;
    static constexpr auto is_struct = !std::is_trivial_v<ValueType> || !std::is_standard_layout_v<ValueType>;
    static constexpr auto is_simple_pod = !is_string && !is_enum && !is_struct;

    ScalarCollector(size_t heartbeat, TinyStrings<>* tiny_strings)
        : CollectableBase(heartbeat)
        , tiny_strings_(tiny_strings)
    {}

    std::string collectableTypeNameForDb() const override final
    {
        return scalarTypeName_<ValueType>();
    }

    template <bool B = is_enum || is_string>
    std::enable_if_t<B, void>
    setValue(const ValueType& value)
    {
        uint32_t string_id = 0;
        if constexpr (is_enum)
        {
            std::ostringstream oss;
            oss << value;
            string_id = tiny_strings_->getStringID(oss.str());
        }
        else
        {
            string_id = tiny_strings_->getStringID(value);
        }
        setValue_(string_id);
    }

    template <bool B = is_simple_pod>
    std::enable_if_t<B, void>
    setValue(const ValueType value)
    {
        setValue_(value);
    }

    void setBytes(const std::vector<char>& bytes)
    {
        sendBytes_(bytes);
    }

private:
    template <typename RawT>
    void setValue_(const RawT value)
    {
        static_assert(std::is_trivial_v<RawT> && std::is_standard_layout_v<RawT> && !std::is_enum_v<RawT>);
        std::vector<char> bytes;
        StreamBuffer buf(bytes);
        buf.append(value);
        sendBytes_(bytes);
    }

    void sendBytes_(const std::vector<char>& bytes)
    {
        // TODO cnyce: refactor so we don't perform the serialization
        // first, only to ignore the bytes because we are not enabled
        if (!enabled())
        {
            return;
        }

        if (quieted())
        {
            awaken();
        }

        CollectedData collected(getID());
        auto& buf = collected.getBuffer();
        buf.append(FULL_ACTION_FLAG); // TODO cnyce: minifiers
        buf.append(bytes);
        stage_(std::move(collected));
    }

    TinyStrings<> *const tiny_strings_;
};

template <typename BinT, bool Sparse>
class ContainerCollector : public CollectableBase
{
public:
    ContainerCollector(size_t heartbeat, size_t capacity)
        : CollectableBase(heartbeat)
        , capacity_(capacity)
    {
        assert(capacity_ <= UINT16_MAX);
        assert(capacity_ > 0);
    }

    std::string collectableTypeNameForDb() const override final
    {
        auto type_name = scalarTypeName_<BinT>();
        if constexpr (Sparse)
        {
            type_name += "_sparse_capacity";
        }
        else
        {
            type_name += "_contig_capacity";
        }
        type_name += std::to_string(capacity_);
        return type_name;
    }

    template <bool sparse = Sparse>
    std::enable_if_t<!sparse, void>
    setBinBytes(const std::vector<std::vector<char>>& bin_bytes)
    {
        if (!enabled())
        {
            return;
        }

        if (quieted())
        {
            awaken();
        }

        CollectedData collected(getID());
        auto& buf = collected.getBuffer();
        buf.append(FULL_ACTION_FLAG); // TODO cnyce: minifiers

        uint64_t size = 0;
        for (auto& bytes : bin_bytes)
        {
            if (!bytes.empty())
            {
                ++size;
            }
            else
            {
                break;
            }
        }

        if (size > UINT16_MAX || size > capacity_)
        {
            throw DBException("Container size cannot exceeed original capacity or UINT16_MAX");
        }

        max_container_size_seen_ = std::max(max_container_size_seen_, (uint16_t)size);
        buf.append((uint16_t)size);
        for (uint16_t bin_idx = 0; bin_idx < (uint16_t)size; ++bin_idx)
        {
            buf.append(bin_bytes[bin_idx]);
        }
        stage_(std::move(collected));
    }

    template <bool sparse = Sparse>
    std::enable_if_t<sparse, void>
    setBinBytes(const std::map<uint16_t, std::vector<char>>& bin_bytes)
    {
        if (!enabled())
        {
            return;
        }

        if (quieted())
        {
            awaken();
        }

        CollectedData collected(getID());
        auto& buf = collected.getBuffer();
        buf.append(FULL_ACTION_FLAG); // TODO cnyce: minifiers

        uint64_t size = 0;
        for (const auto& [cid, bytes] : bin_bytes)
        {
            if (!bytes.empty())
            {
                ++size;
            }
        }

        if (size > UINT16_MAX || size > capacity_)
        {
            throw DBException("Container size cannot exceeed original capacity or UINT16_MAX");
        }

        max_container_size_seen_ = std::max(max_container_size_seen_, (uint16_t)size);
        buf.append((uint16_t)size);
        for (const auto& [cid, bytes] : bin_bytes)
        {
            buf.append(cid);
            buf.append(bytes);
        }
        stage_(std::move(collected));
    }

    void writeMetaOnPostTeardown(DatabaseManager* db_mgr) const override final
    {
        db_mgr->INSERT(
            SQL_TABLE("QueueMaxSizes"),
            SQL_VALUES((int)getID(), (int)max_container_size_seen_));
    }

private:
    const size_t capacity_;
    uint16_t max_container_size_seen_ = 0;
};
#endif

} // namespace simdb::argos
