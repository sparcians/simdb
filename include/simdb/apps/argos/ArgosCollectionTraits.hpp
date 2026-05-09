// <ArgosCollectionTraits.hpp> -*- C++ -*-
/// Traits for detecting nested Argos registrars (\c ArgosCollector, \c ArgosContainerCollector)
/// on collected types, shared by schema construction and collection wiring.

#pragma once

#include "simdb/utils/TypeTraits.hpp"

#include <type_traits>

namespace simdb::collection::detail {

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

/// \c true when \c U names a type that has a nested \c ArgosCollector.
template <typename U, typename = void>
struct has_member_argos_collector : std::false_type
{};

template <typename U>
struct has_member_argos_collector<U, std::void_t<typename U::ArgosCollector>> : std::true_type
{};

/// \c true when \c U names a type that has a nested \c ArgosContainerCollector.
template <typename U, typename = void>
struct has_member_argos_container_collector : std::false_type
{};

template <typename U>
struct has_member_argos_container_collector<U, std::void_t<typename U::ArgosContainerCollector>>
    : std::true_type
{};

/// Whether \c T (after stripping pointers / smart pointers) defines \c ArgosCollector.
template <typename T>
struct has_argos_collector
    : has_member_argos_collector<type_traits::remove_any_pointer_t<T>>
{};

template <typename T>
inline constexpr bool has_argos_collector_v = has_argos_collector<T>::value;

/// Whether the cv/ref-qualified \c T defines nested \c ArgosCollector (used after getter/nested-type normalization).
template <typename T>
struct has_nested_argos_collector : has_member_argos_collector<remove_cvref_t<T>>
{};

template <typename T>
inline constexpr bool has_nested_argos_collector_v = has_nested_argos_collector<T>::value;

/// Whether \c T defines \c ArgosContainerCollector for container default-hidden column metadata.
template <typename T>
struct has_nested_argos_container_collector : has_member_argos_container_collector<remove_cvref_t<T>>
{};

template <typename T>
inline constexpr bool has_nested_argos_container_collector_v =
    has_nested_argos_container_collector<T>::value;

} // namespace simdb::collection::detail
