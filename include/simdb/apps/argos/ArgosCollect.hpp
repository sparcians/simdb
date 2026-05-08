#pragma once

#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace simdb::collection {

class ArgosFieldBase
{
public:
    virtual ~ArgosFieldBase() = default;
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const { return ""; }
    virtual std::string getTypeName() const = 0;
    virtual bool isEnumField() const = 0;
    virtual bool isStructField() const = 0;
    virtual PodTypeKind getPodTypeKind() const = 0;
    virtual EnumBackingKind getEnumBackingKind() const = 0;
    virtual int64_t getEnumValueErased(const void*) const { return 0; }
    virtual std::string getStructTypeName() const = 0;
    virtual std::vector<const ArgosFieldBase*> getStructFields() const = 0;
    virtual SpecialFormatters getSpecialFormatter() const { return None; }
    virtual void writeBufferErased(StreamBuffer&, const void*) const = 0;
    virtual std::string getValueStringErased(const void*) const = 0;
    virtual const void* getStructPtrErased(const void*) const = 0;
    virtual void setTinyStrings(TinyStrings<>*) {}

    /// For color-key inheritance: flattened leaf field name proposed by nested struct collectors only.
    virtual std::string inheritedColorKeyFromNestedCollector() const { return {}; }

    size_t requiredBytes() const
    {
        return requiredBytes_(this);
    }

private:
    static size_t requiredBytes_(const ArgosFieldBase* field)
    {
        if (field->isEnumField())
        {
            return enumBackingKindToBytes(field->getEnumBackingKind());
        }
        else if (field->isStructField())
        {
            size_t struct_bytes = 0;
            for (const auto member : field->getStructFields())
            {
                struct_bytes += requiredBytes_(member);
            }
            return struct_bytes;
        }
        else
        {
            return podKindToBytes(field->getPodTypeKind());
        }
    }
};

template <typename CollectedT>
class ArgosCollectorBase
{
public:
    using collected_type = CollectedT;

    const std::vector<const ArgosFieldBase*>& getFields() const
    {
        return fields_;
    }

    void addField_(const ArgosFieldBase* f)
    {
        fields_.push_back(f);
    }

    /// Called from \c ARGOS_COLOR_KEY registrar; at most one per collector.
    void setColorKeyField_(const char* name)
    {
        if (name == nullptr || *name == '\0')
        {
            return;
        }
        if (!color_key_field_.empty())
        {
            throw DBException("Duplicate ARGOS_COLOR_KEY registration");
        }
        color_key_field_ = name;
    }

    const std::string& getColorKeyField() const { return color_key_field_; }
    bool hasColorKeyField() const { return !color_key_field_.empty(); }

private:
    std::string color_key_field_;
    std::vector<const ArgosFieldBase*> fields_;
};

/// Base for container-wrapper default-hidden column metadata (\c ARGOS_FILTER on nested
/// \c ArgosContainerCollector). Does not participate in serialization; see \c ElemT::ArgosCollector.
class ArgosFilteredCollectorBase
{
public:
    void registerDefaultHiddenField_(const char* field_name)
    {
        if (field_name != nullptr && *field_name != '\0')
        {
            default_hidden_.emplace_back(field_name);
        }
    }

    const std::vector<std::string>& getDefaultHiddenFieldNames() const { return default_hidden_; }

    std::string defaultHiddenColumnsCommaSeparatedForDb() const
    {
        std::string out;
        for (const auto& n : default_hidden_)
        {
            if (!out.empty())
            {
                out += ',';
            }
            out += n;
        }
        return out;
    }

private:
    std::vector<std::string> default_hidden_;
};

template <typename ElemT>
class ArgosFilteredCollector : public ArgosFilteredCollectorBase
{
public:
    using element_type = ElemT;

    void validateDefaultHiddenAgainstElement_() const
    {
        const auto& hidden = getDefaultHiddenFieldNames();
        if (hidden.empty())
        {
            return;
        }
        static const std::unordered_set<std::string> allowed = [] {
            auto hier = createDataTypeHier<ElemT>();
            std::unordered_set<std::string> names;
            detail::collectFlatTableColumnNames(hier->getRoot(), names);
            return names;
        }();
        for (const auto& h : hidden)
        {
            if (!allowed.count(h))
            {
                throw DBException("ARGOS_FILTER: unknown field name: ") << h;
            }
        }
        if (hidden.size() >= allowed.size())
        {
            throw DBException(
                "ARGOS_FILTER: at least one column must remain visible by default (cannot hide all)");
        }
    }
};

template <typename ElemT>
struct ArgosDefaultHiddenFieldReg
{
    explicit ArgosDefaultHiddenFieldReg(ArgosFilteredCollector<ElemT>* owner, const char* field_name)
    {
        owner->registerDefaultHiddenField_(field_name);
    }
};

namespace detail {


template <auto Getter>
struct getter_traits;

template <typename OwnerT, typename RetT, RetT (OwnerT::*Getter)() const>
struct getter_traits<Getter>
{
    using owner_t = OwnerT;
    using return_t = RetT;
};

template <typename OwnerT, typename RetT, RetT (OwnerT::*Getter)()>
struct getter_traits<Getter>
{
    using owner_t = OwnerT;
    using return_t = RetT;
};

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T, typename = void>
struct has_nested_argos_collector : std::false_type {};

template <typename T>
struct has_nested_argos_collector<T, std::void_t<typename remove_cvref_t<T>::ArgosCollector>> : std::true_type
{};

template <typename T>
inline constexpr bool has_nested_argos_collector_v = has_nested_argos_collector<T>::value;


template <typename T, typename = void>
struct has_nested_argos_container_collector : std::false_type {};

template <typename T>
struct has_nested_argos_container_collector<
    T,
    std::void_t<typename remove_cvref_t<T>::ArgosContainerCollector>> : std::true_type
{};

template <typename T>
inline constexpr bool has_nested_argos_container_collector_v =
    has_nested_argos_container_collector<T>::value;

template <typename T, typename = void>
struct has_ostream_insertion : std::false_type {};

template <typename T>
struct has_ostream_insertion<
    T,
    std::void_t<decltype(std::declval<std::ostringstream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_ostream_insertion_v = has_ostream_insertion<T>::value;

// Stringification helpers for ARGOS_STRINGIFY: descend into std::pair / std::tuple, and
// fall back to operator<< for everything else. The recursion is structural so every
// element in the pair/tuple only needs to provide operator<<.
template <typename T>
struct is_std_pair : std::false_type {};

template <typename A, typename B>
struct is_std_pair<std::pair<A, B>> : std::true_type {};

template <typename T>
struct is_std_tuple : std::false_type {};

template <typename... Ts>
struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
std::string argos_stringify(const T& value);

template <typename Tuple, std::size_t... Is>
std::string argos_stringify_tuple_impl(const Tuple& t, std::index_sequence<Is...>)
{
    std::ostringstream oss;
    oss << '(';
    std::size_t i = 0;
    auto append = [&](const auto& v) {
        if (i++ > 0)
        {
            oss << ", ";
        }
        oss << argos_stringify(v);
    };
    (append(std::get<Is>(t)), ...);
    oss << ')';
    return oss.str();
}

template <typename T>
std::string argos_stringify(const T& value)
{
    using bare = remove_cvref_t<T>;
    if constexpr (is_std_pair<bare>::value)
    {
        return std::string{"("} + argos_stringify(value.first)
             + ", " + argos_stringify(value.second) + ")";
    }
    else if constexpr (is_std_tuple<bare>::value)
    {
        return argos_stringify_tuple_impl(
            value,
            std::make_index_sequence<std::tuple_size<bare>::value>{});
    }
    else if constexpr (std::is_same_v<bare, std::string>)
    {
        return value;
    }
    else if constexpr (std::is_pointer_v<bare> &&
                       std::is_same_v<std::remove_cv_t<std::remove_pointer_t<bare>>, char>)
    {
        return value ? std::string{value} : std::string{};
    }
    else
    {
        static_assert(has_ostream_insertion_v<bare>,
                      "ARGOS_STRINGIFY requires every leaf element to provide operator<<");
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
}

// Bare getter return type (reference stripped) -> nested struct type for ArgosStructField.
template <typename BareRet>
struct argos_struct_nested_type {
    using stripped = std::remove_cv_t<BareRet>;
    using type = std::conditional_t<type_traits::is_any_pointer_v<stripped>,
                                    type_traits::remove_any_pointer_t<stripped>,
                                    stripped>;
};

template <typename T>
struct argos_struct_nested_type<std::shared_ptr<T>> {
    using type = T;
};

// Other owning pointer types need matching argos_struct_nested_type and
// is_smart_pointer specializations in namespace simdb::collection::detail (see
// simdb::utils TypeTraits.hpp for is_any_pointer / remove_any_pointer).

template <typename T>
struct is_smart_pointer : std::false_type {};

template <typename T>
struct is_smart_pointer<std::shared_ptr<T>> : std::true_type {};

template <typename T, class Deleter>
struct is_smart_pointer<std::unique_ptr<T, Deleter>> : std::true_type {};

template <typename T>
inline constexpr bool is_smart_ptr_v = is_smart_pointer<T>::value;

template <typename CollectedT>
struct ArgosColorKeyReg
{
    explicit ArgosColorKeyReg(ArgosCollectorBase<CollectedT>* owner, const char* field_name)
    {
        owner->setColorKeyField_(field_name);
    }
};

} // namespace detail

// POD-only field implementation: invokes a getter and memcpy's returned scalar bytes.
template <typename OwnerT, auto Getter>
class ArgosPodField final : public ArgosFieldBase
{
    using traits = detail::getter_traits<Getter>;
    using raw_return_t = typename traits::return_t;
    using value_t = detail::remove_cvref_t<raw_return_t>;

public:
    ArgosPodField(
        ArgosCollectorBase<OwnerT>* owner,
        const char* name,
        const char* description = nullptr,
        SpecialFormatters formatter = None)
        : name_(name)
        , type_name_(demangle_type<value_t>())
        , description_(description && *description ? std::string{description} : std::string{})
        , special_formatter_(formatter)
    {
        initialize_(owner);
    }

    ArgosPodField(
        ArgosCollectorBase<OwnerT>* owner,
        const char* name,
        SpecialFormatters formatter)
        : name_(name)
        , type_name_(demangle_type<value_t>())
        , special_formatter_(formatter)
    {
        initialize_(owner);
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getTypeName() const override { return type_name_; }
    bool isEnumField() const override { return false; }
    bool isStructField() const override { return false; }
    PodTypeKind getPodTypeKind() const override { return detail::getPodTypeKind<value_t>(); }

    EnumBackingKind getEnumBackingKind() const override { return EnumBackingKind::i32; }
    std::string getStructTypeName() const override { return {}; }
    std::vector<const ArgosFieldBase*> getStructFields() const override { return {}; }
    SpecialFormatters getSpecialFormatter() const override { return special_formatter_; }

    void writeBufferErased(StreamBuffer& buffer, const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        if constexpr (std::is_same_v<value_t, std::string>)
        {
            if (tiny_strings_ == nullptr)
            {
                throw DBException("TinyStrings not set before string collection");
            }
            const uint32_t id = tiny_strings_->getStringID(std::invoke(Getter, owner));
            buffer.append(id);
        }
        else if constexpr (std::is_pointer_v<value_t> &&
                           std::is_same_v<std::remove_cv_t<std::remove_pointer_t<value_t>>, char>)
        {
            if (tiny_strings_ == nullptr)
            {
                throw DBException("TinyStrings not set before string collection");
            }
            const char* cstr = std::invoke(Getter, owner);
            const uint32_t id = tiny_strings_->getStringID(cstr ? std::string{cstr} : std::string{});
            buffer.append(id);
        }
        else if constexpr (std::is_same_v<value_t, bool>)
        {
            const uint8_t v = std::invoke(Getter, owner) ? 1u : 0u;
            buffer.append(v);
        }
        else
        {
            value_t v = static_cast<value_t>(std::invoke(Getter, owner));
            buffer.append(v);
        }
    }

    std::string getValueStringErased(const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        std::ostringstream oss;
        if constexpr (std::is_same_v<value_t, std::string>)
        {
            oss << std::invoke(Getter, owner);
        }
        else if constexpr (std::is_pointer_v<value_t> &&
                           std::is_same_v<std::remove_cv_t<std::remove_pointer_t<value_t>>, char>)
        {
            const char* cstr = std::invoke(Getter, owner);
            oss << (cstr ? cstr : "");
        }
        else if constexpr (std::is_same_v<value_t, bool>)
        {
            oss << (std::invoke(Getter, owner) ? "true" : "false");
        }
        else
        {
            oss << std::invoke(Getter, owner);
        }
        return oss.str();
    }

    const void* getStructPtrErased(const void*) const override { return nullptr; }
    void setTinyStrings(TinyStrings<>* tiny_strings) override { tiny_strings_ = tiny_strings; }

private:
    void initialize_(ArgosCollectorBase<OwnerT>* owner)
    {
        static_assert(!std::is_enum_v<value_t>, "ArgosPodField only supports POD (non-enum) fields");
        if (special_formatter_ == HEX)
        {
            if constexpr (std::is_same_v<value_t, std::string> ||
                          (std::is_pointer_v<value_t> &&
                           std::is_same_v<std::remove_cv_t<std::remove_pointer_t<value_t>>, char>) ||
                          std::is_same_v<value_t, bool> ||
                          std::is_floating_point_v<value_t> ||
                          !std::is_integral_v<value_t>)
            {
                throw DBException("HEX formatter is only supported for integral POD fields");
            }
        }
        owner->addField_(this);
    }

private:
    std::string name_;
    std::string type_name_;
    std::string description_;
    SpecialFormatters special_formatter_ = None;
    TinyStrings<>* tiny_strings_ = nullptr;
};

// Enum: getter returns enum type; bytes are the underlying integral representation.
template <typename OwnerT, auto Getter>
class ArgosEnumField final : public ArgosFieldBase
{
    using traits = detail::getter_traits<Getter>;
    using enum_t = detail::remove_cvref_t<typename traits::return_t>;
    using int_t = std::underlying_type_t<enum_t>;

public:
    ArgosEnumField(
        ArgosCollectorBase<OwnerT>* owner,
        const char* name,
        const char* description = nullptr,
        SpecialFormatters formatter = None)
        : name_(name)
        , type_name_(demangle_type<enum_t>())
        , description_(description && *description ? std::string{description} : std::string{})
    {
        initialize_(owner, formatter);
    }

    ArgosEnumField(
        ArgosCollectorBase<OwnerT>* owner,
        const char* name,
        SpecialFormatters formatter)
        : name_(name)
        , type_name_(demangle_type<enum_t>())
    {
        initialize_(owner, formatter);
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getTypeName() const override { return type_name_; }
    bool isEnumField() const override { return true; }
    bool isStructField() const override { return false; }
    PodTypeKind getPodTypeKind() const override { return PodTypeKind::i32; }
    EnumBackingKind getEnumBackingKind() const override { return simdb::collection::getEnumBackingKind<int_t>(); }
    int64_t getEnumValueErased(const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        return static_cast<int64_t>(static_cast<int_t>(std::invoke(Getter, owner)));
    }
    std::string getStructTypeName() const override { return {}; }
    std::vector<const ArgosFieldBase*> getStructFields() const override { return {}; }

    void writeBufferErased(StreamBuffer& buffer, const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        const enum_t enum_value = static_cast<enum_t>(std::invoke(Getter, owner));
        const int_t raw = static_cast<int_t>(enum_value);
        buffer.observeEnum(enum_value, type_name_);
        buffer.append(raw);
    }

    std::string getValueStringErased(const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        const enum_t value = static_cast<enum_t>(std::invoke(Getter, owner));
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    const void* getStructPtrErased(const void*) const override { return nullptr; }

private:
    void initialize_(ArgosCollectorBase<OwnerT>* owner, const SpecialFormatters formatter)
    {
        static_assert(std::is_enum_v<enum_t>, "ArgosEnumField requires an enum getter return type");
        static_assert(
            detail::has_ostream_insertion_v<enum_t>,
            "Enum fields require operator<< to produce string names for enum definitions");
        if (formatter != None)
        {
            throw DBException("Enum fields do not support special formatters");
        }
        owner->addField_(this);
    }

private:
    std::string name_;
    std::string type_name_;
    std::string description_;
};

// Nested aggregate: getter returns const Nested&, Nested*, or std::shared_ptr<Nested>
// (by value or const ref). Nested must define nested ArgosCollector : ArgosCollectorBase<Nested>.
// For shared_ptr-by-value getters, the pointee must stay alive after the temporary smart
// pointer is destroyed (e.g. still held by a member on OwnerT).
template <typename OwnerT, auto Getter>
class ArgosStructField final : public ArgosFieldBase
{
    using traits = detail::getter_traits<Getter>;
    using raw_ret = typename traits::return_t;
    using bare_ret = std::remove_reference_t<raw_ret>;
    using nested_t = typename detail::argos_struct_nested_type<bare_ret>::type;
    using stripped_ret = std::remove_cv_t<bare_ret>;

public:
    ArgosStructField(ArgosCollectorBase<OwnerT>* owner, const char* name, const char* description = nullptr)
        : name_(name)
        , struct_type_name_(demangle_type<nested_t>())
        , description_(description && *description ? std::string{description} : std::string{})
    {
        static_assert(!std::is_enum_v<nested_t>, "Use ARGOS_COLLECT for enum fields");
        static_assert(detail::has_nested_argos_collector_v<nested_t>,
                      "Nested type must define nested ArgosCollector");
        owner->addField_(this);
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getTypeName() const override { return struct_type_name_; }
    bool isEnumField() const override { return false; }
    bool isStructField() const override { return true; }
    PodTypeKind getPodTypeKind() const override { return PodTypeKind::i32; }
    EnumBackingKind getEnumBackingKind() const override { return EnumBackingKind::i32; }
    std::string getStructTypeName() const override { return struct_type_name_; }

    std::vector<const ArgosFieldBase*> getStructFields() const override
    {
        static typename nested_t::ArgosCollector nested_schema;
        return nested_schema.getFields();
    }

    std::string inheritedColorKeyFromNestedCollector() const override
    {
        static typename nested_t::ArgosCollector nested_collector;
        if (nested_collector.hasColorKeyField())
        {
            return nested_collector.getColorKeyField();
        }
        for (const auto* member : nested_collector.getFields())
        {
            if (member == nullptr || !member->isStructField())
            {
                continue;
            }
            std::string k = member->inheritedColorKeyFromNestedCollector();
            if (!k.empty())
            {
                return k;
            }
        }
        return {};
    }

    void writeBufferErased(StreamBuffer&, const void*) const override {}
    std::string getValueStringErased(const void*) const override { return "<struct>"; }

    const void* getStructPtrErased(const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        if constexpr (std::is_pointer_v<stripped_ret>)
        {
            return std::invoke(Getter, owner);
        }
        else if constexpr (detail::is_smart_ptr_v<stripped_ret>)
        {
            return std::invoke(Getter, owner).get();
        }
        else
        {
            // Invoking the getter here yields an lvalue into `*owner`; do not
            // assign it to `auto` (that would copy temporaries and dangle).
            return static_cast<const void*>(std::addressof(std::invoke(Getter, owner)));
        }
    }

private:
    std::string name_;
    std::string struct_type_name_;
    std::string description_;
};

// Stringified field (\c ARGOS_STRINGIFY): the getter may return any type whose elements
// (recursively, for std::pair / std::tuple) provide \c operator<<. The string is interned
// via \ref TinyStrings and serialized as a uint32 string id, matching std::string fields.
template <typename OwnerT, auto Getter>
class ArgosStringifiedField final : public ArgosFieldBase
{
    using traits = detail::getter_traits<Getter>;
    using raw_return_t = typename traits::return_t;
    using value_t = detail::remove_cvref_t<raw_return_t>;

public:
    ArgosStringifiedField(
        ArgosCollectorBase<OwnerT>* owner,
        const char* name,
        const char* description = nullptr)
        : name_(name)
        , type_name_(demangle_type<value_t>())
        , description_(description && *description ? std::string{description} : std::string{})
    {
        static_assert(!std::is_enum_v<value_t>,
                      "ARGOS_STRINGIFY does not support raw enum getters; use ARGOS_COLLECT");
        static_assert(
            detail::has_ostream_insertion_v<value_t> ||
            detail::is_std_pair<value_t>::value ||
            detail::is_std_tuple<value_t>::value ||
            std::is_same_v<value_t, std::string>,
            "ARGOS_STRINGIFY requires the getter return type (or every leaf inside it) "
            "to provide operator<<");
        owner->addField_(this);
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getTypeName() const override { return type_name_; }
    bool isEnumField() const override { return false; }
    bool isStructField() const override { return false; }
    PodTypeKind getPodTypeKind() const override { return PodTypeKind::str; }
    EnumBackingKind getEnumBackingKind() const override { return EnumBackingKind::i32; }
    std::string getStructTypeName() const override { return {}; }
    std::vector<const ArgosFieldBase*> getStructFields() const override { return {}; }
    SpecialFormatters getSpecialFormatter() const override { return None; }

    void writeBufferErased(StreamBuffer& buffer, const void* owner_void) const override
    {
        if (tiny_strings_ == nullptr)
        {
            throw DBException("TinyStrings not set before stringified collection");
        }
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        const std::string s = detail::argos_stringify(std::invoke(Getter, owner));
        const uint32_t id = tiny_strings_->getStringID(s);
        buffer.append(id);
    }

    std::string getValueStringErased(const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        return detail::argos_stringify(std::invoke(Getter, owner));
    }

    const void* getStructPtrErased(const void*) const override { return nullptr; }
    void setTinyStrings(TinyStrings<>* tiny_strings) override { tiny_strings_ = tiny_strings; }

private:
    std::string name_;
    std::string type_name_;
    std::string description_;
    TinyStrings<>* tiny_strings_ = nullptr;
};

namespace detail {

template <typename OwnerT, auto Getter>
using auto_field_t = std::conditional_t<
    std::is_enum_v<remove_cvref_t<typename getter_traits<Getter>::return_t>>,
    ArgosEnumField<OwnerT, Getter>,
    ArgosPodField<OwnerT, Getter>>;

} // namespace detail

} // namespace simdb::collection

// Macro glue
#define ARGOS_COLLECT_CAT_(a, b) a##b
#define ARGOS_COLLECT_CAT(a, b)  ARGOS_COLLECT_CAT_(a, b)

#define ARGOS_COLLECT_SELECT(_1, _2, _3, _4, IMPL, ...) IMPL

// Scalar field: registers one getter-based scalar field in the owning ArgosCollector.
// Enums are auto-routed to ArgosEnumField; all other scalar types use ArgosPodField.
// Optional third argument: const char* description OR simdb::collection::SpecialFormatters.
// Optional fourth argument: simdb::collection::SpecialFormatters.
#define ARGOS_COLLECT(...)                                                                    \
    ARGOS_COLLECT_SELECT(__VA_ARGS__, ARGOS_COLLECT_4, ARGOS_COLLECT_3, ARGOS_COLLECT_2)(__VA_ARGS__)

#define ARGOS_COLLECT_2(field_name, getter_ptr)                                               \
    simdb::collection::detail::auto_field_t<collected_type, getter_ptr>                       \
        ARGOS_COLLECT_CAT(argos_collect_field_, __COUNTER__){this, #field_name, nullptr}

#define ARGOS_COLLECT_3(field_name, getter_ptr, desc)                                         \
    simdb::collection::detail::auto_field_t<collected_type, getter_ptr>                       \
        ARGOS_COLLECT_CAT(argos_collect_field_, __COUNTER__){this, #field_name, desc}

#define ARGOS_COLLECT_4(field_name, getter_ptr, desc, formatter)                              \
    simdb::collection::detail::auto_field_t<collected_type, getter_ptr>                       \
        ARGOS_COLLECT_CAT(argos_collect_field_, __COUNTER__){this, #field_name, desc, formatter}

/// Optional: default table color-key column (flattened leaf field name; at most one per collected type).

#define ARGOS_FILTER_NARG_(...) ARGOS_FILTER_NARG__(__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define ARGOS_FILTER_NARG__(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, n, ...) n
#define ARGOS_FILTER_NARG(...) ARGOS_FILTER_NARG_(__VA_ARGS__)

#define ARGOS_DHF(field)                                                                 \
    ::simdb::collection::ArgosDefaultHiddenFieldReg<element_type> ARGOS_COLLECT_CAT(       \
        argos_dhf_, __COUNTER__)                                                         \
    {                                                                                      \
        this, #field                                                                       \
    };

#define ARGOS_FILTER_1(f0) ARGOS_DHF(f0)
#define ARGOS_FILTER_2(f0, f1) ARGOS_DHF(f0) ARGOS_DHF(f1)
#define ARGOS_FILTER_3(f0, f1, f2) ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2)
#define ARGOS_FILTER_4(f0, f1, f2, f3) ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3)
#define ARGOS_FILTER_5(f0, f1, f2, f3, f4) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4)
#define ARGOS_FILTER_6(f0, f1, f2, f3, f4, f5) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5)
#define ARGOS_FILTER_7(f0, f1, f2, f3, f4, f5, f6) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5) ARGOS_DHF(f6)
#define ARGOS_FILTER_8(f0, f1, f2, f3, f4, f5, f6, f7) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5) ARGOS_DHF(f6) \
        ARGOS_DHF(f7)
#define ARGOS_FILTER_9(f0, f1, f2, f3, f4, f5, f6, f7, f8) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5) ARGOS_DHF(f6) \
        ARGOS_DHF(f7) ARGOS_DHF(f8)
#define ARGOS_FILTER_10(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5) ARGOS_DHF(f6) \
        ARGOS_DHF(f7) ARGOS_DHF(f8) ARGOS_DHF(f9)
#define ARGOS_FILTER_11(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5) ARGOS_DHF(f6) \
        ARGOS_DHF(f7) ARGOS_DHF(f8) ARGOS_DHF(f9) ARGOS_DHF(f10)
#define ARGOS_FILTER_12(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
    ARGOS_DHF(f0) ARGOS_DHF(f1) ARGOS_DHF(f2) ARGOS_DHF(f3) ARGOS_DHF(f4) ARGOS_DHF(f5) ARGOS_DHF(f6) \
        ARGOS_DHF(f7) ARGOS_DHF(f8) ARGOS_DHF(f9) ARGOS_DHF(f10) ARGOS_DHF(f11)

/// Opt-out default-hidden table columns for a nested \c ArgosContainerCollector (wrapper only).
#define ARGOS_FILTER(...) \
    ARGOS_COLLECT_CAT(ARGOS_FILTER_, ARGOS_FILTER_NARG(__VA_ARGS__))(__VA_ARGS__)


#define ARGOS_COLOR_KEY(field_name)                                                           \
    ::simdb::collection::detail::ArgosColorKeyReg<collected_type>                              \
        ARGOS_COLLECT_CAT(argos_color_key_, __COUNTER__){this, #field_name}

#define ARGOS_COLLECT_STRUCT(field_name, getter_ptr)                                          \
    simdb::collection::ArgosStructField<collected_type, getter_ptr>                           \
        ARGOS_COLLECT_CAT(argos_collect_struct_, __COUNTER__){this, #field_name}

// Do not route flatten through ARGOS_COLLECT_STRUCT("", ...): #"" stringizes to a non-empty
// name (the quotes become part of the field name), which breaks duplicate-field suppression
// across nested ARGOS_FLATTEN (every flatten looked like the same bogus name).
#define ARGOS_FLATTEN(getter_ptr)                                                             \
    simdb::collection::ArgosStructField<collected_type, getter_ptr>                           \
        ARGOS_COLLECT_CAT(argos_collect_struct_, __COUNTER__){this, ""}

// Stringified scalar field: the getter may return std::pair<...>, std::tuple<...>, or any
// type whose leaf elements provide operator<<. The composed string is interned via
// TinyStrings and serialized as a uint32 string id (same wire shape as a std::string field).
#define ARGOS_STRINGIFY_SELECT(_1, _2, _3, IMPL, ...) IMPL

#define ARGOS_STRINGIFY(...)                                                                  \
    ARGOS_STRINGIFY_SELECT(__VA_ARGS__, ARGOS_STRINGIFY_3, ARGOS_STRINGIFY_2)(__VA_ARGS__)

#define ARGOS_STRINGIFY_2(field_name, getter_ptr)                                             \
    simdb::collection::ArgosStringifiedField<collected_type, getter_ptr>                      \
        ARGOS_COLLECT_CAT(argos_collect_stringify_, __COUNTER__){this, #field_name, nullptr}

#define ARGOS_STRINGIFY_3(field_name, getter_ptr, desc)                                       \
    simdb::collection::ArgosStringifiedField<collected_type, getter_ptr>                      \
        ARGOS_COLLECT_CAT(argos_collect_stringify_, __COUNTER__){this, #field_name, desc}
