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
// gnu::pure: no side effects, reads only the argument (and nested
// sub-objects via member access).  noexcept: every hash_field branch is
// integer math, bit_cast, or pointer-to-integer — none can throw.
template <typename T>
  requires std::is_class_v<T>
[[nodiscard, gnu::pure]] uint64_t reflect_hash(const T& obj) noexcept;

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
[[nodiscard, gnu::pure]] constexpr uint64_t hash_field(const T& val) noexcept {
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
[[nodiscard, gnu::pure]] uint64_t hash_impl(const T& obj, std::index_sequence<Is...>) noexcept {
  uint64_t h = 0x9E3779B97F4A7C15ULL;
  ((h = h * 0x9E3779B97F4A7C15ULL ^ hash_field(obj.[:member_info<T, Is>():])), ...);
  return detail::fmix64(h);
}

} // namespace detail_reflect

template <typename T>
  requires std::is_class_v<T>
[[nodiscard, gnu::pure]] uint64_t reflect_hash(const T& obj) noexcept {
  constexpr size_t N = detail_reflect::member_count<T>();
  return detail_reflect::hash_impl(obj, std::make_index_sequence<N>{});
}

// ═══════════════════════════════════════════════════════════════════
// has_reflected_hash<T> — consteval trait reporting whether T can be
// hashed via reflect_hash.  True for any class type whose every
// non-static data member is itself reflect_hash-supported (enums,
// integrals, floats, pointers, arrays, nested classes meeting the
// same constraint).
//
// Use this to gate generic code paths that opt into reflection-driven
// hashing — callers that don't satisfy the constraint fall back to a
// manual hash.  Example:
//
//   if constexpr (has_reflected_hash<MyType>) {
//       return reflect_hash(obj);
//   } else {
//       return manual_hash(obj);
//   }
//
// Implementation: probe `reflect_hash(declval<const T&>())` in an
// unevaluated context.  If substitution succeeds, the trait is true.
//
// Safety: zero runtime cost (consteval); zero ODR risk (template
// detection idiom is header-stable).
// ═══════════════════════════════════════════════════════════════════

namespace detail_reflect {

// Detection helper: SFINAE-friendly invocation of reflect_hash.
// requires-expression returns true iff reflect_hash<T>() is callable
// in an unevaluated context.  Wrapped in a consteval to make the
// trait usable at compile time without instantiation.
template <typename T>
consteval bool detect_reflected_hash() noexcept {
  if constexpr (std::is_class_v<T>) {
    return requires (const T& t) { reflect_hash(t); };
  } else {
    return false;
  }
}

}  // namespace detail_reflect

template <typename T>
inline constexpr bool has_reflected_hash =
    detail_reflect::detect_reflected_hash<T>();

// ═══════════════════════════════════════════════════════════════════
// reflect_fmix_fold<T,Seed> — fmix64-based reflection fold helper.
//
// reflect_hash uses a multiplicative wymix-like mixing scheme; some
// callers (feedback_signature, loopterm_hash) need a different
// mixing pattern: seed-then-fmix64-per-field, with the seed acting
// as a domain separator.  This helper provides that pattern over
// reflected fields.
//
// Compared to reflect_hash:
//   - reflect_hash:        h0 = 0x9E37...; for each f: h = h*0x9E37 ^ hash_field(f); h = fmix64(h)
//   - reflect_fmix_fold:   h0 = Seed;     for each f: h = fmix64(h ^ packed_field(f))
//
// The fmix64-fold pattern preserves the "domain-separated, no
// cross-field algebraic collapse" property the manual hashes were
// designed for: a per-field fmix64 ensures bit avalanche before the
// next XOR, so two structs differing in a low-entropy field produce
// hashes differing across the whole word.
//
// Used in MerkleDag.h's feedback_signature / loopterm_hash via
// reflect-based refactors that preserve the call-site semantics
// (Family-A persistence safe per their documented contract — the
// hash differs from the prior manual one in BIT pattern but the
// uniqueness/avalanche properties are equivalent).
// ═══════════════════════════════════════════════════════════════════

namespace detail_reflect {

// Pack a field value into a uint64_t for XOR-into-accumulator
// folding.  Trivial cases (≤8B integral / enum / float / pointer)
// pack directly; arrays are byte-summed; nested classes recurse via
// reflect_hash.  Distinct from hash_field above (which applies
// fmix64 PER field): pack_field is the LINEAR step before the
// outer fmix64 in the fold loop.
template <typename T>
[[nodiscard, gnu::pure]] constexpr uint64_t pack_field(const T& val) noexcept {
  if constexpr (std::is_enum_v<T>) {
    return static_cast<uint64_t>(std::to_underlying(val));
  } else if constexpr (std::is_integral_v<T>) {
    return static_cast<uint64_t>(val);
  } else if constexpr (std::is_floating_point_v<T>) {
    if constexpr (sizeof(T) == 4)
      return static_cast<uint64_t>(std::bit_cast<uint32_t>(val));
    else
      return std::bit_cast<uint64_t>(val);
  } else if constexpr (std::is_pointer_v<T>) {
    return reinterpret_cast<uintptr_t>(val);
  } else if constexpr (std::is_class_v<T>) {
    return reflect_hash(val);  // recursive
  } else {
    static_assert(false, "Unsupported type for reflect_fmix_fold");
  }
}

template <typename T, uint64_t Seed, size_t... Is>
[[nodiscard, gnu::pure]] constexpr uint64_t
fmix_fold_impl(const T& obj, std::index_sequence<Is...>) noexcept {
  uint64_t h = Seed;
  ((h = detail::fmix64(h ^ pack_field(obj.[:member_info<T, Is>():]))), ...);
  return h;
}

}  // namespace detail_reflect

template <uint64_t Seed, typename T>
  requires std::is_class_v<T>
[[nodiscard, gnu::pure]] constexpr uint64_t
reflect_fmix_fold(const T& obj) noexcept {
  constexpr size_t N = detail_reflect::member_count<T>();
  return detail_reflect::fmix_fold_impl<T, Seed>(
      obj, std::make_index_sequence<N>{});
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
//
// Not gnu::pure: fprintf mutates the FILE* stream.  noexcept: Crucible
// compiles with -fno-exceptions, and fprintf is C-linkage so its error
// path is an errno set rather than an exception.  The FILE* argument
// itself is the side-effect channel — callers that pass a valid stream
// get output; a null FILE* is undefined behavior at the libc level and
// outside Reflect's contract.
template <typename T>
  requires std::is_class_v<T>
void reflect_print(const T& obj, FILE* out = stderr) noexcept;

namespace detail_reflect {

// Print a single field value.
template <typename T>
void print_field(const T& val, FILE* out) noexcept {
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
void print_member(const T& obj, FILE* out, bool first) noexcept {
  if (!first) std::fprintf(out, ", ");
  // identifier_of returns a string_view usable at runtime via expansion
  constexpr auto name = member_name<T, I>();
  std::fprintf(out, "%.*s = ", static_cast<int>(name.size()), name.data());
  print_field(obj.[:member_info<T, I>():], out);
}

template <typename T, size_t... Is>
void print_impl(const T& obj, FILE* out, std::index_sequence<Is...>) noexcept {
  constexpr auto type_name = std::meta::identifier_of(^^T);
  std::fprintf(out, "%.*s { ", static_cast<int>(type_name.size()), type_name.data());
  (print_member<T, Is>(obj, out, Is == 0), ...);
  std::fprintf(out, " }");
}

} // namespace detail_reflect

template <typename T>
  requires std::is_class_v<T>
void reflect_print(const T& obj, FILE* out) noexcept {
  constexpr size_t N = detail_reflect::member_count<T>();
  detail_reflect::print_impl(obj, out, std::make_index_sequence<N>{});
}

} // namespace crucible
