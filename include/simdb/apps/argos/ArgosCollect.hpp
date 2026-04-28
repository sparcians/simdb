#pragma once

#include "simdb/apps/argos/DataTypeHierarchy.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
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
    virtual std::vector<EnumMember> getEnumMembers() const = 0;
    virtual std::string getStructTypeName() const = 0;
    virtual std::vector<const ArgosFieldBase*> getStructFields() const = 0;
    virtual void writeBufferErased(StreamBuffer&, const void*) const = 0;
    virtual const void* getStructPtrErased(const void*) const = 0;
    virtual void setTinyStrings(TinyStrings<>*) {}

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

private:
    std::vector<const ArgosFieldBase*> fields_;
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

} // namespace detail

// POD-only field implementation: invokes a getter and memcpy's returned scalar bytes.
template <typename OwnerT, auto Getter>
class ArgosPodField final : public ArgosFieldBase
{
    using traits = detail::getter_traits<Getter>;
    using raw_return_t = typename traits::return_t;
    using value_t = detail::remove_cvref_t<raw_return_t>;

public:
    ArgosPodField(ArgosCollectorBase<OwnerT>* owner, const char* name, const char* description = nullptr)
        : name_(name)
        , type_name_(demangle_type<value_t>())
        , description_(description && *description ? std::string{description} : std::string{})
    {
        static_assert(!std::is_enum_v<value_t>, "ArgosPodField only supports POD (non-enum) fields");
        owner->addField_(this);
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getTypeName() const override { return type_name_; }
    bool isEnumField() const override { return false; }
    bool isStructField() const override { return false; }
    PodTypeKind getPodTypeKind() const override { return detail::getPodTypeKind<value_t>(); }

    EnumBackingKind getEnumBackingKind() const override { return EnumBackingKind::i32; }
    std::vector<EnumMember> getEnumMembers() const override { return {}; }
    std::string getStructTypeName() const override { return {}; }
    std::vector<const ArgosFieldBase*> getStructFields() const override { return {}; }

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

    const void* getStructPtrErased(const void*) const override { return nullptr; }
    void setTinyStrings(TinyStrings<>* tiny_strings) override { tiny_strings_ = tiny_strings; }

private:
    std::string name_;
    std::string type_name_;
    std::string description_;
    TinyStrings<>* tiny_strings_ = nullptr;
};

// Enum: getter returns enum type; bytes are the underlying integral representation.
// Symbol names / values for the schema come from EnumDescriptor<EnumT>::members().
template <typename OwnerT, auto Getter>
class ArgosEnumField final : public ArgosFieldBase
{
    using traits = detail::getter_traits<Getter>;
    using enum_t = detail::remove_cvref_t<typename traits::return_t>;
    using int_t = std::underlying_type_t<enum_t>;

public:
    ArgosEnumField(ArgosCollectorBase<OwnerT>* owner, const char* name, const char* description = nullptr)
        : name_(name)
        , type_name_(demangle_type<enum_t>())
        , description_(description && *description ? std::string{description} : std::string{})
    {
        static_assert(std::is_enum_v<enum_t>, "ArgosEnumField requires an enum getter return type");
        owner->addField_(this);
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getTypeName() const override { return type_name_; }
    bool isEnumField() const override { return true; }
    bool isStructField() const override { return false; }
    PodTypeKind getPodTypeKind() const override { return PodTypeKind::i32; }
    EnumBackingKind getEnumBackingKind() const override { return detail::getBackingKind<int_t>(); }
    std::vector<EnumMember> getEnumMembers() const override { return EnumDescriptor<enum_t>::members(); }
    std::string getStructTypeName() const override { return {}; }
    std::vector<const ArgosFieldBase*> getStructFields() const override { return {}; }

    void writeBufferErased(StreamBuffer& buffer, const void* owner_void) const override
    {
        const auto* owner = static_cast<const OwnerT*>(owner_void);
        const int_t raw = static_cast<int_t>(std::invoke(Getter, owner));
        buffer.append(raw);
    }

    const void* getStructPtrErased(const void*) const override { return nullptr; }

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
    std::vector<EnumMember> getEnumMembers() const override { return {}; }
    std::string getStructTypeName() const override { return struct_type_name_; }

    std::vector<const ArgosFieldBase*> getStructFields() const override
    {
        static typename nested_t::ArgosCollector nested_schema;
        return nested_schema.getFields();
    }

    void writeBufferErased(StreamBuffer&, const void*) const override {}

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

#define ARGOS_COLLECT_SELECT(_1, _2, _3, IMPL, ...) IMPL

// Scalar field: registers one getter-based scalar field in the owning ArgosCollector.
// Enums are auto-routed to ArgosEnumField; all other scalar types use ArgosPodField.
// Optional third argument: const char* description string literal (or other const char*).
#define ARGOS_COLLECT(...)                                                                    \
    ARGOS_COLLECT_SELECT(__VA_ARGS__, ARGOS_COLLECT_3, ARGOS_COLLECT_2)(__VA_ARGS__)

#define ARGOS_COLLECT_2(field_name, getter_ptr)                                               \
    simdb::collection::detail::auto_field_t<collected_type, getter_ptr>                       \
        ARGOS_COLLECT_CAT(argos_collect_field_, __COUNTER__){this, #field_name, nullptr}

#define ARGOS_COLLECT_3(field_name, getter_ptr, desc)                                         \
    simdb::collection::detail::auto_field_t<collected_type, getter_ptr>                       \
        ARGOS_COLLECT_CAT(argos_collect_field_, __COUNTER__){this, #field_name, desc}

#define ARGOS_COLLECT_STRUCT(field_name, getter_ptr)                                          \
    simdb::collection::ArgosStructField<collected_type, getter_ptr>                           \
        ARGOS_COLLECT_CAT(argos_collect_struct_, __COUNTER__){this, #field_name}

#define ARGOS_FLATTEN(...) ARGOS_COLLECT_STRUCT("", __VA_ARGS__)
