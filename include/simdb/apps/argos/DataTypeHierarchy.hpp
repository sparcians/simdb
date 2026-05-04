#pragma once

#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/MoveOnlyFunction.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/StreamBuffer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
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

enum class EnumBackingKind
{
    i8,
    ui8,
    i16,
    ui16,
    i32,
    ui32,
    i64,
    ui64
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

struct EnumMember
{
    std::string name;
    int64_t value = 0; // TODO cnyce: should this be a string?
};

struct EnumMeta
{
    EnumBackingKind backing_kind = EnumBackingKind::i32;
    std::vector<EnumMember> members;
};

inline std::string enum_raw_display_string(const std::vector<EnumMember>& members, const int64_t raw)
{
    for (const auto& member : members)
    {
        if (member.value == raw)
        {
            return member.name;
        }
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

inline std::string enumBackingKindToString(EnumBackingKind kind)
{
    switch (kind)
    {
    case EnumBackingKind::i8: return demangle_type<int8_t>();
    case EnumBackingKind::ui8: return demangle_type<uint8_t>();
    case EnumBackingKind::i16: return demangle_type<int16_t>();
    case EnumBackingKind::ui16: return demangle_type<uint16_t>();
    case EnumBackingKind::i32: return demangle_type<int32_t>();
    case EnumBackingKind::ui32: return demangle_type<uint32_t>();
    case EnumBackingKind::i64: return demangle_type<int64_t>();
    case EnumBackingKind::ui64: return demangle_type<uint64_t>();
}
    throw DBException("Unknown enum backing kind");
    return "";
}

inline size_t enumBackingKindToBytes(EnumBackingKind kind)
{
    switch (kind)
    {
    case EnumBackingKind::i8:
    case EnumBackingKind::ui8:
        return sizeof(int8_t);
    case EnumBackingKind::i16:
    case EnumBackingKind::ui16:
        return sizeof(uint16_t);
    case EnumBackingKind::i32:
    case EnumBackingKind::ui32:
        return sizeof(uint32_t);
    case EnumBackingKind::i64:
    case EnumBackingKind::ui64:
        return sizeof(uint64_t);
    }
    throw DBException("Unknown enum backing kind");
    return 0;
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

template <typename IntT>
constexpr EnumBackingKind getBackingKind()
{
    static_assert(std::is_integral_v<IntT>, "IntT must be integral");
    static_assert(!std::is_same_v<IntT, bool>, "bool is not a valid enum backing type");

    if constexpr (std::is_same_v<IntT, int8_t>)   return EnumBackingKind::i8;
    if constexpr (std::is_same_v<IntT, uint8_t>)  return EnumBackingKind::ui8;
    if constexpr (std::is_same_v<IntT, int16_t>)  return EnumBackingKind::i16;
    if constexpr (std::is_same_v<IntT, uint16_t>) return EnumBackingKind::ui16;
    if constexpr (std::is_same_v<IntT, int32_t>)  return EnumBackingKind::i32;
    if constexpr (std::is_same_v<IntT, uint32_t>) return EnumBackingKind::ui32;
    if constexpr (std::is_same_v<IntT, int64_t>)  return EnumBackingKind::i64;
    if constexpr (std::is_same_v<IntT, uint64_t>) return EnumBackingKind::ui64;
    if constexpr (std::is_signed_v<IntT>)         return EnumBackingKind::i64;
    return EnumBackingKind::i32;
}

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

} // namespace detail

template <typename EnumT>
struct EnumDescriptor
{
    static std::vector<EnumMember> members()
    {
        static_assert(type_traits::always_false_v<EnumT>, "Must specialize this template for your enum");
        return {};
    }
};

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
        FieldTraceSink* field_trace_sink = nullptr) const
    {
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
        FieldTraceSink* field_trace_sink = nullptr) const
    {
        if (value)
        {
            writeBuffer(buffer, *value, expected_num_bytes, field_trace_sink);
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
        node.enum_meta->backing_kind = detail::getBackingKind<enum_int_t>();
        node.enum_meta->members = EnumDescriptor<value_t>::members();
        node.write_erased = [&node](StreamBuffer& buffer, const void* value_void, FieldTraceSink*) {
            const auto* value = static_cast<const value_t*>(value_void);
            const auto raw = static_cast<enum_int_t>(*value);
            const int64_t raw_i64 = static_cast<int64_t>(raw);
            const std::string val_str = enum_raw_display_string(node.enum_meta->members, raw_i64);
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
                    child->enum_meta->members = field->getEnumMembers();

                    child->write_erased = [field, child_field_name = child->field_name](StreamBuffer& buffer, const void* parent_void, FieldTraceSink* field_trace_sink) {
                        auto before = buffer.size();
                        field->writeBufferErased(buffer, parent_void);
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
