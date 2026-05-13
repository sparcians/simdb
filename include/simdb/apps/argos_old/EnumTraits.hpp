#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/utils/Demangle.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

namespace simdb::collection {

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
}

template <typename IntT>
constexpr EnumBackingKind getEnumBackingKind()
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

} // namespace simdb::collection
