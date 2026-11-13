#pragma once

#include <crucible/Expr.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

namespace detail_reflect {

// The two field helpers below each carry an `if constexpr` chain whose
// arms must cover exactly this concept. Their trailing static_assert names
// the concept so a half-finished extension is reported against it.  A
// pointer is outside the concept: its value is an address, and an address
// changes from run to run.
template <typename T>
concept IsReflectFieldSupported = std::is_enum_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T>
                               || std::is_array_v<T> || std::is_class_v<T>;

}  // namespace detail_reflect

// Declared ahead of the field helpers because they recurse back into it.
template <typename T>
    requires std::is_class_v<T>
[[nodiscard, gnu::pure]] uint64_t reflect_hash(const T& obj) noexcept;

namespace detail_reflect {

template <typename T>
[[nodiscard, gnu::pure]] constexpr uint64_t hash_field(const T& val) noexcept {
    if constexpr (std::is_enum_v<T>) {
        return detail::fmix64(static_cast<uint64_t>(std::to_underlying(val)));
    } else if constexpr (std::is_integral_v<T>) {
        return detail::fmix64(static_cast<uint64_t>(val));
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == 4)
            return detail::fmix64(static_cast<uint64_t>(::fixy::bit_cast<uint32_t>(val)));
        else
            return detail::fmix64(::fixy::bit_cast<uint64_t>(val));
    } else if constexpr (std::is_pointer_v<T>) {
        static_assert(false, "reflect_hash refuses a pointer field. An address changes from run to run, so its "
                             "hash breaks the bit-exact replay. Hash the value or the id that the field names");
    } else if constexpr (std::is_array_v<T>) {
        uint64_t h = 0;
        for (size_t i = 0; i < std::extent_v<T>; i++)
            h = h * 0x100000001b3ULL ^ hash_field(val[i]);
        return detail::fmix64(h);
    } else if constexpr (std::is_class_v<T>) {
        return reflect_hash(val);
    } else {
        static_assert(false, "T is outside IsReflectFieldSupported: hash_field handles enum, integral, "
                             "floating-point, array and class types only");
    }
}

}  // namespace detail_reflect

// Folds each non-static data member of T, a private one too, because a
// strong ID keeps its value in a private field.  The members fold in the
// order of the declarations.
template <typename T>
    requires std::is_class_v<T>
[[nodiscard, gnu::pure]] uint64_t reflect_hash(const T& obj) noexcept {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    ::fixy::for_each_field(
        obj, [&h](auto const& field) noexcept { h = h * 0x9E3779B97F4A7C15ULL ^ detail_reflect::hash_field(field); });
    return detail::fmix64(h);
}

namespace detail_reflect {

template <typename T>
consteval bool detect_reflected_hash() noexcept {
    if constexpr (std::is_class_v<T>) {
        return requires(const T& t) { reflect_hash(t); };
    } else {
        return false;
    }
}

}  // namespace detail_reflect

template <typename T>
inline constexpr bool has_reflected_hash = detail_reflect::detect_reflected_hash<T>();

namespace detail_reflect {

// Unlike hash_field, this does no mixing. The fold that consumes it applies
// fmix64 once per field after the xor, so the avalanche happens there.
template <typename T>
[[nodiscard, gnu::pure]] constexpr uint64_t pack_field(const T& val) noexcept {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<uint64_t>(std::to_underlying(val));
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<uint64_t>(val);
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == 4)
            return static_cast<uint64_t>(::fixy::bit_cast<uint32_t>(val));
        else
            return ::fixy::bit_cast<uint64_t>(val);
    } else if constexpr (std::is_pointer_v<T>) {
        static_assert(false, "reflect_fmix_fold refuses a pointer field. An address changes from run to run, so its "
                             "hash breaks the bit-exact replay. Fold the value or the id that the field names");
    } else if constexpr (std::is_array_v<T>) {
        // The same reduction hash_field applies to an array, minus the
        // trailing fmix64.  Keeping the two folds identical up to that
        // last step is what makes an array field order-sensitive in
        // both schemes, and the omission is what keeps this helper's
        // "does no mixing" contract: the consuming fold supplies the
        // avalanche once per field.
        uint64_t h = 0;
        for (size_t i = 0; i < std::extent_v<T>; i++)
            h = h * 0x100000001b3ULL ^ pack_field(val[i]);
        return h;
    } else if constexpr (std::is_class_v<T>) {
        return reflect_hash(val);
    } else {
        static_assert(false, "T is outside IsReflectFieldSupported: pack_field handles enum, integral, "
                             "floating-point, array and class types only");
    }
}

}  // namespace detail_reflect

// A second mixing scheme alongside reflect_hash. The seed is a domain
// separator, and the per-field fmix64 forces a full avalanche before the next
// xor, so two objects differing only in a low-entropy field still differ
// across the whole word.
template <uint64_t Seed, typename T>
    requires std::is_class_v<T>
[[nodiscard, gnu::pure]] constexpr uint64_t reflect_fmix_fold(const T& obj) noexcept {
    uint64_t h = Seed;
    ::fixy::for_each_field(
        obj, [&h](auto const& field) noexcept { h = detail::fmix64(h ^ detail_reflect::pack_field(field)); });
    return h;
}

}  // namespace crucible
