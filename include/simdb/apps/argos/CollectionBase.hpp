// <CollectionBase.hpp> -*- C++ -*-

#pragma once

#include "simdb/schema/SchemaDef.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/utils/TinyStrings.hpp"

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
    virtual void connectToPipeline(ConcurrentQueue<QueueCollectionData>* pipeline_head) = 0;
    virtual TinyStrings<>* getTinyStrings() const = 0;
    virtual void sendCollectedDataToPipeline() = 0;
    virtual bool minifiersSawAllActions() const = 0;
    virtual void writeMetaOnPostTeardown(DatabaseManager* db_mgr) = 0;
};

} // namespace simdb::collection
