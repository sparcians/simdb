// <CollectionBase.hpp> -*- C++ -*-

#pragma once

#include "simdb/schema/SchemaDef.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/apps/argos/EnumDefinitions.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <string>

namespace simdb::collection {

/// \class CollectionBase
/// \brief Base class for all collections (type-specific time values)
class CollectionBase
{
public:
    virtual ~CollectionBase() = default;
    virtual size_t getHeartbeat() const = 0;
    virtual SqlDataType getSqlTimeType() const = 0;
    virtual void writeMetaOnPostInit(DatabaseManager* db_mgr) = 0;
    virtual void connectToPipeline(
        ConcurrentQueue<QueueCollectionData>* pipeline_head,
        EnumDefinitions* enum_definitions) = 0;
    virtual TinyStrings<>* getTinyStrings() const = 0;
    virtual void sendCollectedDataToPipeline() = 0;
    virtual bool minifiersSawAllActions() const = 0;
    virtual void writeMetaOnPostTeardown(DatabaseManager* db_mgr) = 0;

    /// Optional collection byte tracing (see \c simdb::utils::CollectionByteTraceSession).
    /// Default: no-op. \c Collection overrides to install a per-instance trace session.
    virtual void enableByteTracer(const std::string& path, bool reopen_mode)
    {
        (void)path;
        (void)reopen_mode;
    }

    void enableByteTracer(bool reopen_mode = false)
    {
        enableByteTracer("simdb_collection.trace", reopen_mode);
    }

    void enableByteTracer(const std::string& path)
    {
        enableByteTracer(path, false);
    }
};

} // namespace simdb::collection
