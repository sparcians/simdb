// <DataTypeSerializer.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/apps/argos/DataTypeInspector.hpp"
#include "simdb/sqlite/Table.hpp"

namespace simdb::collection {

/// \brief Persists \ref DataTypeInspector metadata (POD leaves, enums, nested structs) into the collection database.
class DataTypeSerializer
{
public:
    static void serialize(DataTypeInspector* inspector, DatabaseManager* db_mgr)
    {
        if (inspector == nullptr || db_mgr == nullptr)
        {
            return;
        }
        db_mgr->safeTransaction([&]()
        {
            Visitor visitor(db_mgr);
            inspector->acceptVisitor(&visitor);
        });
    }

private:
    class Visitor : public DataTypeNodeVisitor
    {
    public:
        explicit Visitor(DatabaseManager* db_mgr) :
            db_mgr_(db_mgr)
        {}

    private:
        void beginRegisteredRootType(const std::string& root_type_name) override
        {
            auto rec =
                db_mgr_->INSERT(SQL_TABLE("DataTypeSchemas"), SQL_VALUES(root_type_name));
            current_schema_id_ = rec->getId();
            parent_node_ids_.clear();
        }

        void endRegisteredRootType(const std::string&) override {}

        void visitSimpleVariable(
            const std::string& var_name,
            const std::string& description,
            PodTypeKind simple_type,
            const std::string& special_formatter = {}) override
        {
            const int32_t parent_id = parent_node_ids_.empty() ? 0 : parent_node_ids_.back();
            const auto pod_name = podTypeKindToTypeName(simple_type);
            db_mgr_->INSERT(SQL_TABLE("DataTypeNodes"),
                            SQL_VALUES(current_schema_id_,
                                       parent_id,
                                       "pod",
                                       var_name,
                                       description,
                                       pod_name,
                                       "",
                                       special_formatter));
        }

        void visitEnumVariable(
            const std::string& enum_name,
            const std::string& description,
            const std::string& enum_type_name,
            EnumBackingKind backing_kind,
            const std::vector<std::pair<std::string, std::string>>& name_value_pairs,
            const std::string& special_formatter = {}) override
        {
            const int32_t parent_id = parent_node_ids_.empty() ? 0 : parent_node_ids_.back();
            auto node_rec =
                db_mgr_->INSERT(SQL_TABLE("DataTypeNodes"),
                                SQL_VALUES(current_schema_id_,
                                           parent_id,
                                           "enum",
                                           enum_name,
                                           description,
                                           enum_type_name,
                                           enumBackingKindToString(backing_kind),
                                           special_formatter));
            const int32_t enum_node_id = node_rec->getId();
            for (const auto& [member_name, member_value] : name_value_pairs)
            {
                (void)db_mgr_->INSERT(SQL_TABLE("DataTypeEnumMembers"),
                                      SQL_VALUES(enum_node_id, member_name, member_value));
            }
        }

        void visitStructVariable(
            const std::string& struct_key,
            const std::string& description,
            const std::string& struct_type_name,
            const std::string& special_formatter = {}) override
        {
            const int32_t parent_id = parent_node_ids_.empty() ? 0 : parent_node_ids_.back();
            auto rec =
                db_mgr_->INSERT(SQL_TABLE("DataTypeNodes"),
                                SQL_VALUES(current_schema_id_,
                                           parent_id,
                                           "struct",
                                           struct_key,
                                           description,
                                           struct_type_name,
                                           "",
                                           special_formatter));
            parent_node_ids_.push_back(rec->getId());
            struct_stack_.push_back(struct_key);
        }

        void endVisitStructVariable(const std::string& struct_key) override
        {
            if (struct_stack_.empty() || struct_stack_.back() != struct_key)
            {
                throw DBException("Struct mismatch in data type visitor. Struct '")
                    << struct_key << "' ended while we were still visiting '"
                    << (struct_stack_.empty() ? std::string{"<empty>"} : struct_stack_.back())
                    << "'.";
            }
            struct_stack_.pop_back();
            parent_node_ids_.pop_back();
        }

        DatabaseManager* db_mgr_;
        int32_t current_schema_id_ = 0;
        std::vector<int32_t> parent_node_ids_;
        std::vector<std::string> struct_stack_;
    };
};

} // namespace simdb::collection
