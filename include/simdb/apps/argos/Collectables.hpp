// <Collectables.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/ArgosResources.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

//! \class CollectionEntryPoint
//! \brief Main entry point into Argos collection.
//!
//! TODO cnyce: These need to be template classes when the collection
//! code from Sparta is moved into SimDB.
class CollectionEntryPoint
{
public:
    CollectionEntryPoint(ArgosResources* resource_container) :
        stager_(resource_container->getStagerResource()),
        tiny_strings_(resource_container->getTinyStringsResource())
    {
    }

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// Enable collection
    void enable()
    {
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

    /// TODO cnyce: Remove this method when Sparta collection code is moved to SimDB.
    /// We can figure this out using <T> only.
    void setScalarDataType(const std::string& dtype)
    {
        assert(collectable_type_name_.empty());
        collectable_type_name_ = dtype;
        stager_->setScalarType(getID());
    }

    /// TODO cnyce: Remove this method when Sparta collection code is moved to SimDB.
    /// We can figure this out using <T> only.
    void setContainerDataType(const std::string& encoded_container_type)
    {
        assert(collectable_type_name_.empty());
        collectable_type_name_ = encoded_container_type;

        size_t pos = encoded_container_type.find_last_not_of("0123456789");
        auto capacity = std::stoi(encoded_container_type.substr(pos + 1));
        assert(capacity <= UINT16_MAX);

        auto sparse = encoded_container_type.find("_sparse") != std::string::npos;
        stager_->setContainerType(getID(), sparse, capacity);
    }

    std::string getEncodedCollectedType() const
    {
        if (collectable_type_name_.empty())
        {
            throw DBException("Collectable data type name never set!");
        }
        return collectable_type_name_;
    }

    //! NOTE: We only have setScalarValueBytes(), setContigContainerBinBytes()
    //! and setSparseContainerBinBytes() all together in one class temporarily
    //! until Sparta/SimDB collection is merged. When the entry point class
    //! becomes a template, these will collapse to one method (<T> decides
    //! the input data structure).
    void setScalarValueBytes(const std::vector<char>& bytes)
    {
        if (enabled())
        {
            stager_->stage(getID(), bytes);
        }
    }

    //! \see setScalarValueBytes
    void setContigContainerBinBytes(const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        if (enabled())
        {
            stager_->stage(getID(), contig_bin_bytes);
        }
    }

    //! \see setScalarValueBytes
    void setSparseContainerBinBytes(const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        if (enabled())
        {
            stager_->stage(getID(), sparse_bin_bytes);
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

    /// Unique collectable ID
    const uint16_t cid_{nextCID_()};

    /// Enabled flag
    bool enabled_ = true;

    /// Suppress heartbeat re-emission while true
    bool quiet_ = false;

    std::string collectable_type_name_;
    PipelineStagerResource& stager_;
    TinyStringsResource& tiny_strings_;
};

} // namespace simdb::argos
