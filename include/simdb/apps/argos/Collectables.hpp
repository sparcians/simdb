// <Collectables.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/ArgosResources.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/PipelineStagerInterface.hpp"
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
    CollectionEntryPoint(PipelineStagerInterface* stager_interface) :
        tiny_strings_(stager_interface->getResources()->getTinyStringsResource()),
        stager_interface_(stager_interface)
    {
    }

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// Suppress heartbeat re-emission of previously seen bytes.
    void quiet()
    {
        if (!quiet_)
        {
            quiet_ = true;
            stager_interface_->recordOpenChange(getID(), quiet_);
        }
    }

    /// Re-enable heartbeat re-emission of previously seen bytes.
    void awaken()
    {
        if (quiet_)
        {
            quiet_ = false;
            stager_interface_->recordOpenChange(getID(), quiet_);
        }
    }

    /// Check enabled
    bool enabled() const { return enabled_; }

    /// Check whether heartbeat re-emission is suppressed.
    bool quieted() const { return quiet_; }

    /// For testing purposes only. DO NOT CALL IN PRODUCTION.
    static void resetCIDs() { nextCID_() = 0; }

    safe_weak_ptr<TinyStrings<>> getTinyStrings() const { return tiny_strings_.get(); }

    //! NOTE: We only have setScalarValueBytes(), setContigContainerBinBytes()
    //! and setSparseContainerBinBytes() all together in one class temporarily
    //! until Sparta/SimDB collection is merged. When the entry point class
    //! becomes a template, these will collapse to one method (<T> decides
    //! the input data structure).
    void setScalarValueBytes(std::vector<char>&& scalar_bytes)
    {
        if (enabled())
        {
            stager_interface_->stage(getID(), std::move(scalar_bytes));
        }
    }

    //! \see setScalarValueBytes
    void setContigContainerBinBytes(std::vector<std::vector<char>>&& contig_bin_bytes)
    {
        if (enabled())
        {
            stager_interface_->stage(getID(), std::move(contig_bin_bytes));
        }
    }

    //! \see setScalarValueBytes
    void setSparseContainerBinBytes(std::map<uint16_t, std::vector<char>>&& sparse_bin_bytes)
    {
        if (enabled())
        {
            stager_interface_->stage(getID(), std::move(sparse_bin_bytes));
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
    TinyStringsResource& tiny_strings_;
    PipelineStagerInterface* const stager_interface_;
};

} // namespace simdb::argos
