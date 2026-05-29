// <Collectables.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/ArgosResources.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

class CollectionEntryPoint
{
public:
    CollectionEntryPoint(ArgosResources* resource_container) :
        stager_(resource_container->getStagerResource()),
        tiny_strings_(resource_container->getTinyStringsResource()),
        argos_resources_(resource_container)
    {
    }

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// Enable collection
    void enable()
    {
        if (!enabled_)
        {
            // TODO XXX: handle initial value on first enable()
            enabled_ = true;
            stager_->onEnabledChanged(getID(), enabled_);
        }
    }

    /// Disable collection
    void disable()
    {
        if (enabled_)
        {
            enabled_ = false;
            stager_->onEnabledChanged(getID(), enabled_);
        }
    }

    /// Suppress heartbeat re-emission of previously seen bytes.
    void quiet()
    {
        if (!quiet_)
        {
            quiet_ = true;
            stager_->onQuietChanged(getID(), quiet_);
        }
    }

    /// Re-enable heartbeat re-emission of previously seen bytes.
    void awaken()
    {
        if (quiet_)
        {
            quiet_ = false;
            stager_->onQuietChanged(getID(), quiet_);
        }
    }

    /// Check enabled
    bool enabled() const { return enabled_; }

    /// Check whether heartbeat re-emission is suppressed.
    bool quieted() const { return quiet_; }

    /// Add a timestamped warning/error/msg which applies to this collectable.
    /// All of these will be visible in the Argos UI. These are purely for
    /// the user's benefit; Argos doesn't do anything with them but give
    /// a modal dialog of these notifications. These are never printed to
    /// stdout/stderr.
    void postNotif(const std::string& notif, NotifType type) { stager_->postNotif(getID(), notif, type); }

    void postWarning(const std::string& warning) { postNotif(warning, NotifType::WARNING); }

    void postError(const std::string& error) { postNotif(error, NotifType::ERROR); }

    void postMessage(const std::string& msg) { postNotif(msg, NotifType::MESSAGE); }

    /// For testing purposes only. DO NOT CALL IN PRODUCTION.
    static void resetCIDs() { nextCID_() = 0; }

    safe_weak_ptr<TinyStrings<>> getTinyStrings() const { return tiny_strings_.get(); }

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

    std::string getEncodedCollectedType() const
    {
        if (collectable_type_name_.empty())
        {
            throw DBException("Collectable data type name never set!");
        }
        return collectable_type_name_;
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

    void sendBytes_(const std::vector<char>& bytes)
    {
        assert(enabled());

        if (quieted())
        {
            awaken();
        }

        auto& collection_data = argos_resources_->getCollectedDataBuffersResource().getFor(getID());
        collection_data.reset();

        auto& buf = collection_data.getBuffer();
        buf.append(FULL_ACTION_FLAG); // TODO cnyce: minifiers
        buf.append(bytes);
        stager_->stage(collection_data);

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

    std::string collectable_type_name_;
    ValidValue<bool> is_scalar_;
    ValidValue<bool> is_contig_container_;
    ValidValue<bool> is_sparse_container_;
    ValidValue<uint16_t> max_container_size_seen_;

    PipelineStagerResource& stager_;
    TinyStringsResource& tiny_strings_;
    ArgosResources* const argos_resources_;
};

} // namespace simdb::argos
