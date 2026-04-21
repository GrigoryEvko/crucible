#pragma once

// Reflect.h: Auto-generated struct hashing and debug printing via
// P2996 static reflection (GCC 16 with -freflection).
//
// GCC 16 is the only supported compiler and always provides reflection
// when built via the project presets.

#include <crucible/Platform.h>
#include <crucible/Expr.h> // detail::fmix64

#include <bit>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <type_traits>
#include <utility>

namespace crucible {

// ═══════════════════════════════════════════════════════════════════
// reflect_hash<T>: Automatic struct hashing via reflection
//
// Iterates all non-static data members, hashes each field with
// fmix64, and combines via multiplicative mixing. Handles:
//   - Integral types + enums → static_cast<uint64_t>
//   - Floating point → bit_cast to uint of same size
//   - Pointers → reinterpret_cast<uintptr_t>
//   - C arrays → hash each element
//   - Nested structs → recursive reflect_hash
//
// Usage:
//   uint64_t h = crucible::reflect_hash(my_guard);
// ═══════════════════════════════════════════════════════════════════

// Forward declaration for recursive nested struct hashing.
template <typename T>
  requires std::is_class_v<T>
[[nodiscard]] uint64_t reflect_hash(const T& obj);

namespace detail_reflect {

// Count non-static data members of T.
template <typename T>
consteval size_t member_count() {
  return std::meta::nonstatic_data_members_of(
      ^^T, std::meta::access_context::unchecked()).size();
}

// Get the I-th non-static data member info.
template <typename T, size_t I>
consteval auto member_info() {
  return std::meta::nonstatic_data_members_of(
      ^^T, std::meta::access_context::unchecked())[I];
}

// Hash a single field value. Dispatches on type.
template <typename T>
[[nodiscard]] constexpr uint64_t hash_field(const T& val) {
  if constexpr (std::is_enum_v<T>) {
    return detail::fmix64(static_cast<uint64_t>(std::to_underlying(val)));
  } else if constexpr (std::is_integral_v<T>) {
    return detail::fmix64(static_cast<uint64_t>(val));
  } else if constexpr (std::is_floating_point_v<T>) {
    if constexpr (sizeof(T) == 4)
      return detail::fmix64(static_cast<uint64_t>(std::bit_cast<uint32_t>(val)));
    else
      return detail::fmix64(std::bit_cast<uint64_t>(val));
  } else if constexpr (std::is_pointer_v<T>) {
    return detail::fmix64(reinterpret_cast<uintptr_t>(val));
  } else if constexpr (std::is_array_v<T>) {
    uint64_t h = 0;
    for (size_t i = 0; i < std::extent_v<T>; i++)
      h = h * 0x100000001b3ULL ^ hash_field(val[i]);
    return detail::fmix64(h);
  } else if constexpr (std::is_class_v<T>) {
    return reflect_hash(val); // recursive
  } else {
    static_assert(false, "Unsupported type for reflect_hash");
  }
}

// Fold over all members via index_sequence.
template <typename T, size_t... Is>
[[nodiscard]] uint64_t hash_impl(const T& obj, std::index_sequence<Is...>) {
  uint64_t h = 0x9E3779B97F4A7C15ULL;
  ((h = h * 0x9E3779B97F4A7C15ULL ^ hash_field(obj.[:member_info<T, Is>():])), ...);
  return detail::fmix64(h);
}

} // namespace detail_reflect

template <typename T>
  requires std::is_class_v<T>
[[nodiscard]] uint64_t reflect_hash(const T& obj) {
  constexpr size_t N = detail_reflect::member_count<T>();
  return detail_reflect::hash_impl(obj, std::make_index_sequence<N>{});
}

// ═══════════════════════════════════════════════════════════════════
// reflect_print<T>: Auto-generated debug printing via reflection
//
// Prints "TypeName { field0 = val, field1 = val, ... }\n" to stderr.
// Handles the same type categories as reflect_hash.
//
// Usage:
//   crucible::reflect_print(my_guard);
// ═══════════════════════════════════════════════════════════════════

// Forward declaration for recursive nested struct printing.
template <typename T>
  requires std::is_class_v<T>
void reflect_print(const T& obj, FILE* out = stderr);

namespace detail_reflect {

// Print a single field value.
template <typename T>
void print_field(const T& val, FILE* out) {
  if constexpr (std::is_enum_v<T>) {
    std::fprintf(out, "%llu", static_cast<unsigned long long>(std::to_underlying(val)));
  } else if constexpr (std::is_same_v<T, bool>) {
    std::fprintf(out, "%s", val ? "true" : "false");
  } else if constexpr (std::is_signed_v<T> && std::is_integral_v<T>) {
    std::fprintf(out, "%lld", static_cast<long long>(val));
  } else if constexpr (std::is_unsigned_v<T> && std::is_integral_v<T>) {
    std::fprintf(out, "%llu", static_cast<unsigned long long>(val));
  } else if constexpr (std::is_floating_point_v<T>) {
    std::fprintf(out, "%g", static_cast<double>(val));
  } else if constexpr (std::is_pointer_v<T>) {
    std::fprintf(out, "%p", static_cast<const void*>(val));
  } else if constexpr (std::is_array_v<T>) {
    std::fprintf(out, "[");
    for (size_t i = 0; i < std::extent_v<T>; i++) {
      if (i > 0) std::fprintf(out, ", ");
      print_field(val[i], out);
    }
    std::fprintf(out, "]");
  } else if constexpr (std::is_class_v<T>) {
    reflect_print(val, out); // recursive
  } else {
    static_assert(false, "Unsupported type for reflect_print");
  }
}

// Get the name of the I-th member as a compile-time string.
template <typename T, size_t I>
consteval auto member_name() {
  return std::meta::identifier_of(member_info<T, I>());
}

// Print one "name = value" pair.
template <typename T, size_t I>
void print_member(const T& obj, FILE* out, bool first) {
  if (!first) std::fprintf(out, ", ");
  // identifier_of returns a string_view usable at runtime via expansion
  constexpr auto name = member_name<T, I>();
  std::fprintf(out, "%.*s = ", static_cast<int>(name.size()), name.data());
  print_field(obj.[:member_info<T, I>():], out);
}

template <typename T, size_t... Is>
void print_impl(const T& obj, FILE* out, std::index_sequence<Is...>) {
  constexpr auto type_name = std::meta::identifier_of(^^T);
  std::fprintf(out, "%.*s { ", static_cast<int>(type_name.size()), type_name.data());
  (print_member<T, Is>(obj, out, Is == 0), ...);
  std::fprintf(out, " }");
}

} // namespace detail_reflect

template <typename T>
  requires std::is_class_v<T>
void reflect_print(const T& obj, FILE* out) {
  constexpr size_t N = detail_reflect::member_count<T>();
  detail_reflect::print_impl(obj, out, std::make_index_sequence<N>{});
}

} // namespace crucible
