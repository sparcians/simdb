#pragma once

#include "simdb/apps/argos/EnumTraits.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/MoveOnlyFunction.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/StreamBuffer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <utility>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace simdb::collection {

class FieldTraceSink;

using WriteErased =
    simdb::utils::MoveOnlyFunction<void(StreamBuffer&, const void*, FieldTraceSink*)>;

class FieldTraceSink
{
public:
    virtual ~FieldTraceSink() = default;
    virtual void beginStructFields(std::string_view, std::size_t) {}
    virtual void endStructFields() {}
    /// \param field_name Field identifier (e.g. \c "uid").
    /// \param dtype_name Demangled/symbolic type (e.g. \c "unsigned int", \c "InstType", \c "string").
    virtual void recordFieldBytes(
        std::size_t,
        std::string_view /*field_name*/,
        std::string_view /*dtype_name*/,
        std::string_view /*value_repr*/) {}
};

enum class NodeKind
{
    Pod,
    Enum,
    Struct
};

enum class PodTypeKind
{
    c,
    i8,
    ui8,
    i16,
    ui16,
    i32,
    ui32,
    i64,
    ui64,
    d,
    f,
    logical,
    str
};

enum SpecialFormatters
{
    None,
    HEX
};

inline std::string specialFormatterToString(SpecialFormatters formatter)
{
    switch (formatter)
    {
    case None: return "";
    case HEX: return "hex";
    }
    throw DBException("Unknown special formatter");
}

inline size_t podKindToBytes(PodTypeKind kind)
{
    switch (kind)
    {
        case PodTypeKind::c:
        case PodTypeKind::i8:
        case PodTypeKind::ui8:
        case PodTypeKind::logical:
            return sizeof(uint8_t);
        case PodTypeKind::i16:
        case PodTypeKind::ui16:
            return sizeof(uint16_t);
        case PodTypeKind::i32:
        case PodTypeKind::ui32:
        case PodTypeKind::f:
        case PodTypeKind::str:
            return sizeof(uint32_t);
        case PodTypeKind::i64:
        case PodTypeKind::ui64:
        case PodTypeKind::d:
            return sizeof(uint64_t);
    }
    throw DBException("Unknown data type");
    return 0;
}

struct EnumMeta
{
    EnumBackingKind backing_kind = EnumBackingKind::i32;
    std::map<int64_t, std::string> raw_to_name;
};

inline std::string enum_raw_display_string(const std::map<int64_t, std::string>& raw_to_name, const int64_t raw)
{
    auto it = raw_to_name.find(raw);
    if (it != raw_to_name.end())
    {
        return it->second;
    }
    std::ostringstream oss;
    oss << raw;
    return oss.str();
}

struct DataTypeNode
{
    NodeKind kind = NodeKind::Pod;
    DataTypeNode* parent = nullptr;
    std::string field_name;
    std::string description;
    std::string type_name;
    /// Resolved default column for auto-color / preferred table ordering (flattened leaf field name).
    std::string effective_color_key;
    std::string special_formatter;
    std::unique_ptr<PodTypeKind> pod_type;
    std::unique_ptr<EnumMeta> enum_meta;
    std::vector<std::unique_ptr<DataTypeNode>> children;
    WriteErased write_erased;

    // Set by DataTypeInspector::connect(). Used by string writers.
    TinyStrings<>* tiny_strings = nullptr;

    // Optional backpointer to the originating field descriptor.
    // DataTypeInspector uses this to inject TinyStrings into field writers.
    void* source_field = nullptr;
};

inline std::string podTypeKindToTypeName(PodTypeKind kind)
{
    switch (kind)
    {
    case PodTypeKind::c: return demangle_type<char>();
    case PodTypeKind::i8: return demangle_type<int8_t>();
    case PodTypeKind::ui8: return demangle_type<uint8_t>();
    case PodTypeKind::i16: return demangle_type<int16_t>();
    case PodTypeKind::ui16: return demangle_type<uint16_t>();
    case PodTypeKind::i32: return demangle_type<int32_t>();
    case PodTypeKind::ui32: return demangle_type<uint32_t>();
    case PodTypeKind::i64: return demangle_type<int64_t>();
    case PodTypeKind::ui64: return demangle_type<uint64_t>();
    case PodTypeKind::d: return demangle_type<double>();
    case PodTypeKind::f: return demangle_type<float>();
    case PodTypeKind::logical: return demangle_type<bool>();
    case PodTypeKind::str: return "string";
    }
    throw DBException("Unknown data type");
    return "";
}

namespace detail {

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

/// Leaf scalars for auto/POD wiring: trivial layout types plus \c std::string (uint32 string id).
/// Enums are \c NodeKind::Enum, not this trait.
template <typename T>
constexpr bool is_pod_leaf_v =
    std::is_same_v<remove_cvref_t<T>, std::string> ||
    (std::is_trivial_v<remove_cvref_t<T>> &&
     std::is_standard_layout_v<remove_cvref_t<T>> &&
     !std::is_enum_v<remove_cvref_t<T>>);

template <typename PodT>
constexpr PodTypeKind getPodTypeKind()
{
    using value_t = remove_cvref_t<PodT>;

    if constexpr (std::is_same_v<value_t, std::string>)
    {
        return PodTypeKind::str;
    }
    else if constexpr (std::is_pointer_v<value_t> &&
                       std::is_same_v<std::remove_cv_t<std::remove_pointer_t<value_t>>, char>)
    {
        return PodTypeKind::str;
    }
    else if constexpr (std::is_same_v<value_t, char>)
    {
        return PodTypeKind::c;
    }
    else if constexpr (std::is_same_v<value_t, double>)
    {
        return PodTypeKind::d;
    }
    else if constexpr (std::is_same_v<value_t, float>)
    {
        return PodTypeKind::f;
    }
    else if constexpr (std::is_same_v<value_t, bool>)
    {
        return PodTypeKind::logical;
    }
    else if constexpr (std::is_integral_v<value_t> && std::is_signed_v<value_t>)
    {
        if constexpr (sizeof(value_t) == 1) return PodTypeKind::i8;
        if constexpr (sizeof(value_t) == 2) return PodTypeKind::i16;
        if constexpr (sizeof(value_t) == 4) return PodTypeKind::i32;
        if constexpr (sizeof(value_t) == 8) return PodTypeKind::i64;
    }
    else if constexpr (std::is_integral_v<value_t> && std::is_unsigned_v<value_t>)
    {
        if constexpr (sizeof(value_t) == 1) return PodTypeKind::ui8;
        if constexpr (sizeof(value_t) == 2) return PodTypeKind::ui16;
        if constexpr (sizeof(value_t) == 4) return PodTypeKind::ui32;
        if constexpr (sizeof(value_t) == 8) return PodTypeKind::ui64;
    }
    else
    {
        static_assert(!std::is_same_v<value_t, value_t>, "Unsupported POD leaf type for DataTypeHierarchy");
    }
}

template <typename T, typename = void>
struct has_argos_collector : std::false_type {};

template <typename T>
struct has_argos_collector<T, std::void_t<typename type_traits::remove_any_pointer_t<T>::ArgosCollector>> : std::true_type {};

template <typename T>
inline constexpr bool has_argos_collector_v = has_argos_collector<T>::value;

/// Root-level \c std::pair handling in \c createDataTypeHier (distinct name from \c ArgosCollect.hpp stringification traits).
template <typename T>
struct is_std_pair_product : std::false_type {};

template <typename A, typename B>
struct is_std_pair_product<std::pair<A, B>> : std::true_type {};

template <typename T>
inline constexpr bool is_std_pair_product_v = is_std_pair_product<remove_cvref_t<T>>::value;

/// After moving a \c DataTypeNode subtree, fix \c parent links (addresses change).
inline void relinkParentPointers(DataTypeNode* subtree_root, DataTypeNode* new_parent)
{
    if (subtree_root == nullptr)
    {
        return;
    }
    subtree_root->parent = new_parent;
    for (const auto& ch : subtree_root->children)
    {
        if (ch)
        {
            relinkParentPointers(ch.get(), subtree_root);
        }
    }
}

/// Names of flattened table columns under a struct \c DataTypeNode (matches struct deserializer leaf order).
inline void collectFlatTableColumnNames(const DataTypeNode& row_node, std::unordered_set<std::string>& out_names)
{
    for (const auto& ch : row_node.children)
    {
        if (!ch)
        {
            continue;
        }
        if (ch->kind == NodeKind::Struct)
        {
            collectFlatTableColumnNames(*ch, out_names);
        }
        else if (ch->kind == NodeKind::Pod || ch->kind == NodeKind::Enum)
        {
            if (!ch->field_name.empty())
            {
                out_names.insert(ch->field_name);
            }
        }
    }
}

/// After \c std::move(root_) from a temporary \c DataTypeHierarchy into another tree, lambdas that
/// captured \c [&node] still reference the moved-from object. Rebind writers to the final \c node.
template <typename T>
void reattachErasedWritersAfterSubtreeSteal(DataTypeNode& node)
{
    using value_t = remove_cvref_t<T>;
    if constexpr (std::is_enum_v<value_t>)
    {
        node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
            const auto* value = static_cast<const value_t*>(value_void);
            using enum_int_t = std::underlying_type_t<value_t>;
            const auto raw = static_cast<enum_int_t>(*value);
            const int64_t raw_i64 = static_cast<int64_t>(raw);
            buffer.observeEnum(*value, node.type_name);
            if (node.enum_meta)
            {
                std::ostringstream oss;
                oss << *value;
                node.enum_meta->raw_to_name.emplace(raw_i64, oss.str());
            }
            const std::string val_str = enum_raw_display_string(node.enum_meta->raw_to_name, raw_i64);
            buffer.appendValue(raw, node.type_name, val_str);
        };
    }
    else if constexpr (has_argos_collector_v<value_t>)
    {
        node.write_erased = [&node](StreamBuffer& buffer,
                                    const void* owner_void,
                                    FieldTraceSink* field_trace_sink) {
            for (const auto& ch : node.children)
            {
                if (ch && ch->write_erased)
                {
                    ch->write_erased(buffer, owner_void, field_trace_sink);
                }
            }
        };
    }
    else if constexpr (is_std_pair_product_v<value_t>)
    {
        reattachErasedWritersAfterSubtreeSteal<typename value_t::first_type>(*node.children[0]);
        reattachErasedWritersAfterSubtreeSteal<typename value_t::second_type>(*node.children[1]);
        DataTypeNode* fn = node.children[0].get();
        DataTypeNode* sn = node.children[1].get();
        node.write_erased = [fn, sn](StreamBuffer& buffer,
                                     const void* pair_void,
                                     FieldTraceSink* field_trace_sink) {
            const auto* p = static_cast<const value_t*>(pair_void);
            if (fn->write_erased)
            {
                fn->write_erased(buffer, &p->first, field_trace_sink);
            }
            if (sn->write_erased)
            {
                sn->write_erased(buffer, &p->second, field_trace_sink);
            }
        };
    }
    else if constexpr (is_pod_leaf_v<value_t>)
    {
        if constexpr (std::is_same_v<value_t, std::string>)
        {
            node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
                if (node.tiny_strings == nullptr)
                {
                    throw DBException("TinyStrings not set before string collection");
                }
                const auto* s = static_cast<const value_t*>(value_void);
                const uint32_t id = node.tiny_strings->getStringID(*s);
                std::ostringstream oss;
                oss << id << ", string value " << *s;
                buffer.appendValue(id, "string id", oss.str());
            };
        }
        else if constexpr (std::is_same_v<value_t, bool>)
        {
            node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
                const auto* value = static_cast<const value_t*>(value_void);
                const uint8_t v = (*value) ? 1u : 0u;
                buffer.appendValue(v, node.type_name, (*value) ? "true" : "false");
            };
        }
        else
        {
            node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
                const auto value = *static_cast<const value_t*>(value_void);
                std::ostringstream oss;
                oss << value;
                buffer.appendValue(value, node.type_name, oss.str());
            };
        }
    }
}

} // namespace detail

class DataTypeHierarchyBase
{
public:
    virtual ~DataTypeHierarchyBase() = default;
    virtual const DataTypeNode& getRoot() const = 0;
};

template <typename RootT>
class DataTypeHierarchy : public DataTypeHierarchyBase
{
public:
    const DataTypeNode& getRoot() const override
    {
        return root_;
    }

    void writeBuffer(
        StreamBuffer& buffer,
        const RootT& value,
        ValidValue<size_t>* expected_num_bytes = nullptr,
        FieldTraceSink* field_trace_sink = nullptr,
        EnumDefinitions* enum_definitions = nullptr) const
    {
        buffer.setEnumDefinitions(enum_definitions);
        if (root_.write_erased)
        {
            auto curr_size = buffer.size();
            root_.write_erased(buffer, &value, field_trace_sink);
            if (expected_num_bytes)
            {
                auto these_bytes = buffer.size() - curr_size;
                if (!expected_num_bytes->isValid())
                {
                    *expected_num_bytes = these_bytes;
                }
                else if (expected_num_bytes->getValue() != these_bytes)
                {
                    throw DBException("Byte mismatch");
                }
            }
        }
    }

    template <typename T>
    std::enable_if_t<type_traits::is_any_pointer_v<T>, void>
    writeBuffer(
        StreamBuffer& buffer,
        const T& value,
        ValidValue<size_t>* expected_num_bytes = nullptr,
        FieldTraceSink* field_trace_sink = nullptr,
        EnumDefinitions* enum_definitions = nullptr) const
    {
        if (value)
        {
            writeBuffer(buffer, *value, expected_num_bytes, field_trace_sink, enum_definitions);
        }
    }

private:
    template <typename T>
    friend std::unique_ptr<DataTypeHierarchy<detail::remove_cvref_t<T>>> createDataTypeHier();

    DataTypeNode root_;
};

template <typename T>
inline std::unique_ptr<DataTypeHierarchy<detail::remove_cvref_t<T>>> createDataTypeHier()
{
    using value_t = detail::remove_cvref_t<T>;
    auto hier = std::make_unique<DataTypeHierarchy<value_t>>();
    auto& node = hier->root_;

    if constexpr (std::is_same_v<value_t, std::string>)
    {
        node.type_name = "string";
    }
    else
    {
        node.type_name = demangle_type<value_t>();
    }

    if constexpr (std::is_enum_v<value_t>)
    {
        node.kind = NodeKind::Enum;
        node.enum_meta = std::make_unique<EnumMeta>();
        using enum_int_t = std::underlying_type_t<value_t>;
        node.enum_meta->backing_kind = getEnumBackingKind<enum_int_t>();
        node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
            const auto* value = static_cast<const value_t*>(value_void);
            const auto raw = static_cast<enum_int_t>(*value);
            const int64_t raw_i64 = static_cast<int64_t>(raw);
            buffer.observeEnum(*value, node.type_name);
            if (node.enum_meta)
            {
                std::ostringstream oss;
                oss << *value;
                node.enum_meta->raw_to_name.emplace(raw_i64, oss.str());
            }
            const std::string val_str = enum_raw_display_string(node.enum_meta->raw_to_name, raw_i64);
            buffer.appendValue(raw, node.type_name, val_str);
        };
    }
    else if constexpr (detail::has_argos_collector_v<value_t>)
    {
        node.kind = NodeKind::Struct;

        std::vector<std::string> active_struct_stack{node.type_name};
        std::unordered_set<std::string> seen_field_names;

        auto populate_children = [&](DataTypeNode& parent,
                                     const auto& fields,
                                     auto&& self) -> WriteErased
        {
            for (const auto* field : fields)
            {
                if (field == nullptr)
                {
                    continue;
                }
                const std::string field_name = field->getName();
                if (!field_name.empty() &&
                    seen_field_names.find(field_name) != seen_field_names.end())
                {
                    // Keep the first field with a given name and suppress all later
                    // duplicates, including flattened nested fields.
                    continue;
                }
                if (!field_name.empty())
                {
                    seen_field_names.insert(field_name);
                }

                auto child = std::make_unique<DataTypeNode>();
                child->parent = &parent;
                child->field_name = field_name;
                child->description = field->getDescription();
                child->type_name = field->getTypeName();
                child->special_formatter = specialFormatterToString(field->getSpecialFormatter());
                child->source_field = const_cast<void*>(static_cast<const void*>(field));

                if (field->isStructField())
                {
                    child->kind = NodeKind::Struct;
                    child->type_name = field->getStructTypeName();

                    if (std::find(active_struct_stack.begin(),
                                  active_struct_stack.end(),
                                  child->type_name) != active_struct_stack.end())
                    {
                        throw DBException(
                            "Recursive struct cycle detected while building data type hierarchy");
                    }

                    active_struct_stack.emplace_back(child->type_name);
                    auto nested_writer = self(*child, field->getStructFields(), self);
                    active_struct_stack.pop_back();

                    child->write_erased = [field, nested_writer = std::move(nested_writer), child_field_name = child->field_name](
                        StreamBuffer& buffer, const void* parent_void, FieldTraceSink* field_trace_sink) {
                        const auto nested_ptr = field->getStructPtrErased(parent_void);
                        if (nested_ptr == nullptr)
                        {
                            return;
                        }
                        if (field_trace_sink)
                        {
                            field_trace_sink->beginStructFields(child_field_name, field->requiredBytes());
                        }
                        nested_writer(buffer, nested_ptr, field_trace_sink);
                        if (field_trace_sink)
                        {
                            field_trace_sink->endStructFields();
                        }
                    };
                }
                else if (field->isEnumField())
                {
                    child->kind = NodeKind::Enum;
                    child->enum_meta = std::make_unique<EnumMeta>();
                    child->enum_meta->backing_kind = field->getEnumBackingKind();

                    child->write_erased = [field, child = child.get(), child_field_name = child->field_name](StreamBuffer& buffer, const void* parent_void, FieldTraceSink* field_trace_sink) {
                        auto before = buffer.size();
                        field->writeBufferErased(buffer, parent_void);
                        if (child && child->enum_meta)
                        {
                            const int64_t raw = field->getEnumValueErased(parent_void);
                            const std::string value_repr = field->getValueStringErased(parent_void);
                            child->enum_meta->raw_to_name.emplace(raw, value_repr);
                        }
                        if (field_trace_sink)
                        {
                            const auto num_bytes = buffer.size() - before;
                            field_trace_sink->recordFieldBytes(
                                num_bytes,
                                child_field_name,
                                field->getTypeName(),
                                field->getValueStringErased(parent_void));
                        }
                    };
                }
                else
                {
                    child->kind = NodeKind::Pod;
                    child->pod_type = std::make_unique<PodTypeKind>(field->getPodTypeKind());

                    child->write_erased = [field, child_field_name = child->field_name](StreamBuffer& buffer, const void* parent_void, FieldTraceSink* field_trace_sink) {
                        auto before = buffer.size();
                        field->writeBufferErased(buffer, parent_void);
                        if (field_trace_sink)
                        {
                            const auto num_bytes = buffer.size() - before;
                            const bool is_string_pod =
                                !field->isEnumField() && !field->isStructField() && field->getPodTypeKind() == PodTypeKind::str;
                            const std::string dtype_name = is_string_pod ? std::string{"string"} : field->getTypeName();
                            std::string trace_value = field->getValueStringErased(parent_void);
                            if (is_string_pod && num_bytes >= sizeof(uint32_t))
                            {
                                uint32_t id = 0;
                                const auto& bytes = buffer.byte_storage();
                                std::memcpy(&id, bytes.data() + before, sizeof id);
                                std::ostringstream oss;
                                oss << "string id: " << id << ", string value: " << field->getValueStringErased(parent_void);
                                trace_value = oss.str();
                            }
                            field_trace_sink->recordFieldBytes(num_bytes, child_field_name, dtype_name, trace_value);
                        }
                    };
                }

                parent.children.emplace_back(std::move(child));
            }

            return [&parent](StreamBuffer& buffer, const void* owner_void, FieldTraceSink* field_trace_sink) {
                for (const auto& ch : parent.children)
                {
                    if (ch->write_erased)
                    {
                        ch->write_erased(buffer, owner_void, field_trace_sink);
                    }
                }
            };
        };

        // IMPORTANT: Writers stored in the hierarchy may capture field pointers.
        // To keep those pointers valid beyond this function, the collector must
        // outlive the returned DataTypeHierarchy. For now, keep one static
        // collector instance per collected type.
        static typename value_t::ArgosCollector collector;
        node.write_erased = populate_children(node,
                                              collector.getFields(),
                                              populate_children);

        std::unordered_set<std::string> flat_column_names;
        detail::collectFlatTableColumnNames(node, flat_column_names);

        std::string color_key;
        if (collector.hasColorKeyField())
        {
            color_key = collector.getColorKeyField();
        }
        else
        {
            for (const auto* field : collector.getFields())
            {
                if (field == nullptr || !field->isStructField())
                {
                    continue;
                }
                color_key = field->inheritedColorKeyFromNestedCollector();
                if (!color_key.empty())
                {
                    break;
                }
            }
        }
        if (!color_key.empty() && flat_column_names.count(color_key) == 0)
        {
            color_key.clear();
        }
        node.effective_color_key = std::move(color_key);
    }
    else if constexpr (detail::is_std_pair_product_v<value_t>)
    {
        node.kind = NodeKind::Struct;

        using first_t = typename value_t::first_type;
        using second_t = typename value_t::second_type;

        auto first_hier = createDataTypeHier<first_t>();
        auto second_hier = createDataTypeHier<second_t>();

        auto first_child = std::make_unique<DataTypeNode>();
        *first_child = std::move(first_hier->root_);
        first_child->field_name = "first";
        detail::relinkParentPointers(first_child.get(), &node);

        auto second_child = std::make_unique<DataTypeNode>();
        *second_child = std::move(second_hier->root_);
        second_child->field_name = "second";
        detail::relinkParentPointers(second_child.get(), &node);

        node.children.emplace_back(std::move(first_child));
        node.children.emplace_back(std::move(second_child));

        detail::reattachErasedWritersAfterSubtreeSteal<first_t>(*node.children[0]);
        detail::reattachErasedWritersAfterSubtreeSteal<second_t>(*node.children[1]);

        DataTypeNode* fn = node.children[0].get();
        DataTypeNode* sn = node.children[1].get();
        node.write_erased = [fn, sn](StreamBuffer& buffer,
                                     const void* pair_void,
                                     FieldTraceSink* field_trace_sink) {
            const auto* p = static_cast<const value_t*>(pair_void);
            if (fn->write_erased)
            {
                fn->write_erased(buffer, &p->first, field_trace_sink);
            }
            if (sn->write_erased)
            {
                sn->write_erased(buffer, &p->second, field_trace_sink);
            }
        };

        node.effective_color_key.clear();
    }
    else if constexpr (detail::is_pod_leaf_v<value_t>)
    {
        node.kind = NodeKind::Pod;
        node.pod_type = std::make_unique<PodTypeKind>(detail::getPodTypeKind<value_t>());
        if constexpr (std::is_same_v<value_t, std::string>)
        {
            node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
                if (node.tiny_strings == nullptr)
                {
                    throw DBException("TinyStrings not set before string collection");
                }
                const auto* s = static_cast<const value_t*>(value_void);
                const uint32_t id = node.tiny_strings->getStringID(*s);
                std::ostringstream oss;
                oss << id << ", string value " << *s;
                buffer.appendValue(id, "string id", oss.str());
            };
        }
        else
        {
            if constexpr (std::is_same_v<value_t, bool>)
            {
                node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
                    const auto* value = static_cast<const value_t*>(value_void);
                    const uint8_t v = (*value) ? 1u : 0u;
                    buffer.appendValue(v, node.type_name, (*value) ? "true" : "false");
                };
            }
            else
            {
                node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
                    const auto value = *static_cast<const value_t*>(value_void);
                    std::ostringstream oss;
                    oss << value;
                    buffer.appendValue(value, node.type_name, oss.str());
                };
            }
        }
    }
    else
    {
        static_assert(detail::has_argos_collector_v<value_t>,
                      "Struct-like types must provide nested ArgosCollector");
    }

    return hier;
}

} // namespace simdb::collection
