#pragma once

#include <crucible/Platform.h>
#include <crucible/Expr.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <type_traits>
#include <utility>

namespace crucible {

namespace detail_reflect {

// The three field helpers below each carry an `if constexpr` chain whose
// arms must cover exactly this concept. Their trailing static_assert names
// the concept so a half-finished extension is reported against it.
template <typename T>
concept IsReflectFieldSupported = std::is_enum_v<T> || std::is_integral_v<T> || std::is_floating_point_v<T>
                               || std::is_pointer_v<T> || std::is_array_v<T> || std::is_class_v<T>;

static_assert(IsReflectFieldSupported<int>, "integral types must satisfy IsReflectFieldSupported");
static_assert(IsReflectFieldSupported<double>, "floating-point types must satisfy IsReflectFieldSupported");
static_assert(IsReflectFieldSupported<void*>, "pointer types must satisfy IsReflectFieldSupported");
static_assert(IsReflectFieldSupported<int[4]>, "C array types must satisfy IsReflectFieldSupported");
struct ReflectFieldSentinel {
    int x;
};
static_assert(IsReflectFieldSupported<ReflectFieldSentinel>,
              "class types must satisfy IsReflectFieldSupported, which is what closes the recursion");

static_assert(!IsReflectFieldSupported<void>, "void is not a reflectable type category");
static_assert(!IsReflectFieldSupported<int(int)>, "function types are not reflectable, but function pointers are");

}  // namespace detail_reflect

// Declared ahead of the field helpers because they recurse back into it.
template <typename T>
    requires std::is_class_v<T>
[[nodiscard, gnu::pure]] uint64_t reflect_hash(const T& obj) noexcept;

namespace detail_reflect {

template <typename T>
consteval size_t member_count() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
}

template <typename T, size_t I>
consteval auto member_info() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())[I];
}

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
        return detail::fmix64(std::bit_cast<uintptr_t>(val));
    } else if constexpr (std::is_array_v<T>) {
        uint64_t h = 0;
        for (size_t i = 0; i < std::extent_v<T>; i++)
            h = h * 0x100000001b3ULL ^ hash_field(val[i]);
        return detail::fmix64(h);
    } else if constexpr (std::is_class_v<T>) {
        return reflect_hash(val);
    } else {
        static_assert(false, "T is outside IsReflectFieldSupported: hash_field handles enum, integral, "
                             "floating-point, pointer, array and class types only");
    }
}

template <typename T, size_t... Is>
[[nodiscard, gnu::pure]] uint64_t hash_impl(const T& obj, std::index_sequence<Is...>) noexcept {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    ((h = h * 0x9E3779B97F4A7C15ULL ^ hash_field(obj.[:member_info<T, Is>():])), ...);
    return detail::fmix64(h);
}

}  // namespace detail_reflect

template <typename T>
    requires std::is_class_v<T>
[[nodiscard, gnu::pure]] uint64_t reflect_hash(const T& obj) noexcept {
    constexpr size_t N = detail_reflect::member_count<T>();
    return detail_reflect::hash_impl(obj, std::make_index_sequence<N>{});
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
            return static_cast<uint64_t>(std::bit_cast<uint32_t>(val));
        else
            return std::bit_cast<uint64_t>(val);
    } else if constexpr (std::is_pointer_v<T>) {
        return std::bit_cast<uintptr_t>(val);
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
                             "floating-point, pointer, array and class types only");
    }
}

template <typename T, uint64_t Seed, size_t... Is>
[[nodiscard, gnu::pure]] constexpr uint64_t fmix_fold_impl(const T& obj, std::index_sequence<Is...>) noexcept {
    uint64_t h = Seed;
    ((h = detail::fmix64(h ^ pack_field(obj.[:member_info<T, Is>():]))), ...);
    return h;
}

}  // namespace detail_reflect

// A second mixing scheme alongside reflect_hash. The seed is a domain
// separator, and the per-field fmix64 forces a full avalanche before the next
// xor, so two objects differing only in a low-entropy field still differ
// across the whole word.
template <uint64_t Seed, typename T>
    requires std::is_class_v<T>
[[nodiscard, gnu::pure]] constexpr uint64_t reflect_fmix_fold(const T& obj) noexcept {
    constexpr size_t N = detail_reflect::member_count<T>();
    return detail_reflect::fmix_fold_impl<T, Seed>(obj, std::make_index_sequence<N>{});
}

// Declared ahead of print_field because it recurses back into it. noexcept
// holds because fprintf has C linkage and reports failure through errno.
template <typename T>
    requires std::is_class_v<T>
void reflect_print(const T& obj, FILE* out = stderr) noexcept;

namespace detail_reflect {

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
        reflect_print(val, out);
    } else {
        static_assert(false, "T is outside IsReflectFieldSupported: print_field handles enum, integral, "
                             "floating-point, pointer, array and class types only");
    }
}

template <typename T, size_t I>
consteval auto member_name() {
    return std::meta::identifier_of(member_info<T, I>());
}

template <typename T, size_t I>
void print_member(const T& obj, FILE* out, bool first) noexcept {
    if (!first) std::fprintf(out, ", ");
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

}  // namespace detail_reflect

template <typename T>
    requires std::is_class_v<T>
void reflect_print(const T& obj, FILE* out) noexcept {
    constexpr size_t N = detail_reflect::member_count<T>();
    detail_reflect::print_impl(obj, out, std::make_index_sequence<N>{});
}

}  // namespace crucible
