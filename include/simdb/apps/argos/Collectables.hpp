// <Collectables.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

class CollectionEntryPoint
{
public:
    CollectionEntryPoint(size_t heartbeat, TinyStrings<>* tiny_strings) :
        tiny_strings_(tiny_strings)
    {
        // TODO cnyce: heartbeat is for the Minifiers once they get added back
        (void)heartbeat;
    }

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// \brief Connect to the ArgosCollector's main input queue
    void connectToPipeline(PipelineStager* stager)
    {
        stager_ = stager;
        if (throw_on_any_activity_)
        {
            stager_->throwOnAnyActivity(getID());
        }
    }

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

    /// Legacy collectors in unit tests often cannot be collected
    /// and seem to only exist to check that compilation succeeds.
    /// If anything touches the PipelineStager with our CID, the
    /// simulation will immediately throw.
    void throwOnAnyActivity()
    {
        throw_on_any_activity_ = true;
        if (stager_)
        {
            stager_->throwOnAnyActivity(getID());
        }
    }

    /// Add a timestamped warning/error/msg which applies to this collectable.
    /// All of these will be visible in the Argos UI. These are purely for
    /// the user's benefit; Argos doesn't do anything with them but give
    /// a modal dialog of these notifications. These are never printed to
    /// stdout/stderr.
    void postNotif(const std::string& notif, NotifType type)
    {
        assert(stager_ != nullptr);
        stager_->postNotif(getID(), notif, type);
    }

    void postWarning(const std::string& warning) { postNotif(warning, NotifType::WARNING); }

    void postError(const std::string& error) { postNotif(error, NotifType::ERROR); }

    void postMessage(const std::string& msg) { postNotif(msg, NotifType::MESSAGE); }

    /// For testing purposes only. DO NOT CALL IN PRODUCTION.
    static void resetCIDs() { nextCID_() = 0; }

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

    std::string encodeTypeName() const
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
            // TODO cnyce: Enums should be written as their underlying int type, and
            // a string-int mapping should be put in the database. For now, just rely
            // on TinyStrings and treat enums as strings instead of ints.
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
        if (!max_container_size_seen_.isValid())
        {
            max_container_size_seen_ = size;
        } else
        {
            max_container_size_seen_ = std::max(max_container_size_seen_.getValue(), (uint16_t)size);
        }

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
        if (!max_container_size_seen_.isValid())
        {
            max_container_size_seen_ = size;
        } else
        {
            max_container_size_seen_ = std::max(max_container_size_seen_.getValue(), (uint16_t)size);
        }

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

    void writeMetaOnPostTeardown(DatabaseManager* db_mgr) const
    {
        if (max_container_size_seen_.isValid())
        {
            db_mgr->INSERT(SQL_TABLE("QueueMaxSizes"), SQL_VALUES((int)getID(), (int)max_container_size_seen_));
        }
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

    /// Stage collected bytes for pipeline processing.
    void stage_(CollectedData&& data)
    {
        if (!stager_) [[unlikely]]
        {
            throw DBException("Pipeline was never opened!");
        }
        stager_->stage(std::move(data));
    }

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

        // This code was working with the previous collector design for Minifiers.hpp
        // to save a ton on disk space:
        // https://github.com/sparcians/simdb/pull/185/changes/5988aa743d015c1f0fa6f16fe9f6b517b191b679#diff-2315254eec5b0d00f82ecc99fd169ee680eaca658a10a54f0f2f5e914131b4a4
        //
        // We'll port the minifier code to the latest design when the non-optimized
        // Argos collector has settled a bit. For now, we'll accept lots of waste by
        // doing full byte dumps each time (FULL_ACTION_FLAG).
    }

    /// Unique collectable ID
    const uint16_t cid_{nextCID_()};

    /// Enabled flag
    bool enabled_ = true;

    /// Suppress heartbeat re-emission while true
    bool quiet_ = false;

    /// Throw if this collectable attempts to access the PipelineStager.
    bool throw_on_any_activity_ = false;

    /// Main entry point into the pipeline
    PipelineStager* stager_ = nullptr;

    std::string collectable_type_name_;
    ValidValue<bool> is_scalar_;
    ValidValue<bool> is_contig_container_;
    ValidValue<bool> is_sparse_container_;
    ValidValue<uint16_t> max_container_size_seen_;
    TinyStrings<>* tiny_strings_ = nullptr;
};

} // namespace simdb::argos
