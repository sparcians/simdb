#pragma once

#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/apps/argos/ArgosCollect.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>

namespace simdb::collection {

class DataTypeNodeVisitor
{
public:
    virtual ~DataTypeNodeVisitor() = default;

    /// \brief Called before traversing each root hierarchy registered with \ref DataTypeInspector::registerType.
    virtual void beginRegisteredRootType(const std::string& root_type_name,
                                         const std::string& effective_color_key = {})
    {
        (void)root_type_name;
        (void)effective_color_key;
    }

    /// \brief Called after traversing a root hierarchy.
    virtual void endRegisteredRootType(const std::string& root_type_name)
    {
        (void)root_type_name;
    }

    virtual void visitSimpleVariable(
        const std::string& var_name,
        const std::string& description,
        PodTypeKind simple_type,
        const std::string& special_formatter = {})
    {
        (void)var_name;
        (void)description;
        (void)simple_type;
        (void)special_formatter;
    }

    virtual void visitEnumVariable(
        const std::string& enum_name,
        const std::string& description,
        const std::string& enum_type_name,
        EnumBackingKind backing_kind,
        const std::vector<std::pair<std::string, std::string>>& name_value_pairs,
        const std::string& special_formatter = {})
    {
        (void)enum_name;
        (void)description;
        (void)enum_type_name;
        (void)backing_kind;
        (void)name_value_pairs;
        (void)special_formatter;
    }

    virtual void visitStructVariable(
        const std::string& struct_key,
        const std::string& description,
        const std::string& struct_type_name,
        const std::string& special_formatter = {})
    {
        (void)struct_key;
        (void)description;
        (void)struct_type_name;
        (void)special_formatter;
    }

    virtual void endVisitStructVariable(const std::string& struct_key)
    {
        (void)struct_key;
    }
};

class DataTypeInspector
{
public:
    ~DataTypeInspector()
    {
        if (tiny_strings_)
        {
            if (auto count = tiny_strings_->getUnserializedCount(); count != 0)
            {
                std::cout << "WARNING: There " << (count == 1 ? "was" : "were")
                          << count << " string" << (count == 1 ? "" : "s")
                          << " not written to the database when a DataTypeInspector"
                          << " was destroyed." << std::endl;
            }
        }
    }

    template <typename Type>
    std::shared_ptr<DataTypeHierarchy<Type>> registerType()
    {
        using value_t = std::remove_cv_t<std::remove_reference_t<Type>>;
        std::string type_name;
        if constexpr (std::is_same_v<value_t, std::string>)
        {
            type_name = "string";
        }
        else
        {
            type_name = simdb::demangle_type<value_t>();
        }
        if (root_hierarchies_.count(type_name))
        {
            return std::dynamic_pointer_cast<DataTypeHierarchy<Type>>(
                root_hierarchies_.at(type_name));
        }

        std::shared_ptr<DataTypeHierarchy<Type>> hier = createDataTypeHier<value_t>();
        root_hierarchies_.emplace(type_name, hier);

        return hier;
    }

    void bindDatabase(DatabaseManager* db_mgr)
    {
        if (db_mgr == nullptr || tiny_strings_)
        {
            return;
        }
        tiny_strings_ = std::make_unique<simdb::TinyStrings<>>(db_mgr);
        for (const auto& [_, hier] : root_hierarchies_)
        {
            injectTinyStringsIntoFields_(hier->getRoot(), tiny_strings_.get());
        }
    }

    TinyStrings<>* getTinyStrings() const
    {
        return tiny_strings_.get();
    }

    void acceptVisitor(DataTypeNodeVisitor* visitor) const
    {
        if (!visitor)
        {
            return;
        }
        for (const auto& [root_type_name, hier] : root_hierarchies_)
        {
            visitor->beginRegisteredRootType(root_type_name, hier->getRoot().effective_color_key);
            visitRecursive_(hier->getRoot(), *visitor);
            visitor->endRegisteredRootType(root_type_name);
        }
    }

    void teardown()
    {
        if (tiny_strings_)
        {
            tiny_strings_->serialize();
        }
    }

private:
    static void visitRecursive_(const DataTypeNode& node,
                                DataTypeNodeVisitor& visitor)
    {
        if (node.kind == NodeKind::Pod && node.pod_type)
        {
            const std::string var_name = node.field_name.empty() ? node.type_name : node.field_name;
            visitor.visitSimpleVariable(
                var_name, node.description, *node.pod_type, node.special_formatter);
        }
        else if (node.kind == NodeKind::Enum && node.enum_meta)
        {
            const std::string enum_name = node.field_name.empty() ? node.type_name : node.field_name;
            std::vector<std::pair<std::string, std::string>> name_value_pairs;
            name_value_pairs.reserve(node.enum_meta->raw_to_name.size());
            for (const auto& [raw_value, member_name] : node.enum_meta->raw_to_name)
            {
                name_value_pairs.emplace_back(member_name, std::to_string(raw_value));
            }
            visitor.visitEnumVariable(enum_name,
                                      node.description,
                                      node.type_name,
                                      node.enum_meta->backing_kind,
                                      name_value_pairs,
                                      node.special_formatter);
        }
        else if (node.kind == NodeKind::Struct)
        {
            const std::string struct_key = node.field_name.empty() ? node.type_name : node.field_name;
            visitor.visitStructVariable(
                struct_key, node.description, node.type_name, node.special_formatter);
            for (const auto& child : node.children)
            {
                visitRecursive_(*child, visitor);
            }
            visitor.endVisitStructVariable(struct_key);
            return;
        }

        for (const auto& child : node.children)
        {
            visitRecursive_(*child, visitor);
        }
    }

    static void injectTinyStringsIntoFields_(const DataTypeNode& node,
                                             TinyStrings<>* tiny_strings)
    {
        if (node.source_field)
        {
            auto* field = static_cast<ArgosFieldBase*>(node.source_field);
            field->setTinyStrings(tiny_strings);
        }
        // Root-level std::string (and any POD-string node without an ArgosField) writes via
        // node.tiny_strings in createDataTypeHier(); only nested fields get source_field set.
        if (node.pod_type && *node.pod_type == PodTypeKind::str)
        {
            const_cast<DataTypeNode&>(node).tiny_strings = tiny_strings;
        }
        for (const auto& child : node.children)
        {
            injectTinyStringsIntoFields_(*child, tiny_strings);
        }
    }

    std::map<std::string, std::shared_ptr<DataTypeHierarchyBase>> root_hierarchies_;
    std::unique_ptr<simdb::TinyStrings<>> tiny_strings_;
};

} // namespace simdb::collection
