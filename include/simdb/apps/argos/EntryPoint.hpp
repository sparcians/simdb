// <EntryPoint.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/EnumInspector.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/PipelineStagerInterface.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

//! \class EntryPoint
//! \brief Main entry point into Argos collection.
//!
//! TODO cnyce: These need to be template classes when the collection
//! code from Sparta is moved into SimDB.
class EntryPoint
{
public:
    EntryPoint(PipelineStagerInterface* stager_interface, TinyStrings<>* tiny_strings) :
        stager_interface_(stager_interface),
        tiny_strings_(tiny_strings)
    {
    }

    /// Get the unique ID for this collection point.
    uint16_t getID() const { return cid_; }

    /// Suppress heartbeat re-emission of previously seen bytes.
    void closeRecord()
    {
        if (!closed_)
        {
            closed_ = true;
            stager_interface_->closeRecord(getID());
        }
    }

    /// For testing purposes only. DO NOT CALL IN PRODUCTION.
    static void resetCIDs() { nextCID_() = 0; }

    TinyStrings<>* getTinyStrings() const { return tiny_strings_; }

    //! NOTE: We only have setScalarValueBytes(), setContigContainerBinBytes()
    //! and setSparseContainerBinBytes() all together in one class temporarily
    //! until Sparta/SimDB collection is merged. When the entry point class
    //! becomes a template, these will collapse to one method (<T> decides
    //! the input data structure).
    void setScalarValueBytes(std::vector<char>&& scalar_bytes)
    {
        closed_ = false;
        stager_interface_->stage(getID(), std::move(scalar_bytes));
    }

    //! \see setScalarValueBytes
    void setContigContainerBinBytes(std::vector<std::vector<char>>&& contig_bin_bytes)
    {
        closed_ = false;
        stager_interface_->stage(getID(), std::move(contig_bin_bytes));
    }

    //! \see setScalarValueBytes
    void setSparseContainerBinBytes(std::map<uint16_t, std::vector<char>>&& sparse_bin_bytes)
    {
        closed_ = false;
        stager_interface_->stage(getID(), std::move(sparse_bin_bytes));
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

    /// Suppress heartbeat re-emission while true
    bool closed_ = false;

    /// Most of what EntryPoint does is forward to the stager (ledger)
    PipelineStagerInterface* const stager_interface_;

    /// DB-backed string-to-int mapping
    TinyStrings<>* const tiny_strings_;
};

} // namespace simdb::argos
