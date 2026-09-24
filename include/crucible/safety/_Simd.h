#pragma once

// A SIMD facade over the compiler's own vector extensions rather than
// the standard <simd> header.  That header is gated behind an x86 macro
// in the standard library shipped here, so it is empty on ARM, while the
// vector extensions compile the same way on both.
//
// The declared lane count is logical.  The build lowers it to whatever
// the target's widest instruction is, and the result does not change,
// because integer element-wise operations give identical lanes at any
// width and integer folds with xor, add, and, or, min and max are both
// associative and commutative.
//
// Floating-point reductions have neither property under IEEE rounding,
// so their result would move with the register width.  The DetSafeSimd
// concept keeps them out of every reduction here.

#include <crucible/Platform.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace crucible::simd {

#if defined(__AVX512F__)
inline constexpr std::size_t native_bytes = 64;
#elif defined(__AVX2__)
inline constexpr std::size_t native_bytes = 32;
#else
inline constexpr std::size_t native_bytes = 16;
#endif

static_assert(native_bytes >= 16, "A 128-bit SIMD register is the assumed floor on every supported target.");

template <typename ElementType>
inline constexpr int native_lane_count = static_cast<int>(native_bytes / sizeof(ElementType));

// The vector-size attribute is silently dropped when applied to a
// dependent alias, so each element-and-lane pair needs a concrete
// typedef behind a trait rather than one alias template.

namespace detail {

template <typename ElementType, int Lanes>
struct raw_vector;

#define CRUCIBLE_SIMD_RAW(ElementType, Lanes)                                                   \
    template <>                                                                                 \
    struct raw_vector<ElementType, Lanes> {                                                     \
        typedef ElementType type __attribute__((__vector_size__(Lanes * sizeof(ElementType)))); \
    }

CRUCIBLE_SIMD_RAW(std::int64_t, 4);
CRUCIBLE_SIMD_RAW(std::int64_t, 8);
CRUCIBLE_SIMD_RAW(std::uint64_t, 4);
CRUCIBLE_SIMD_RAW(std::uint64_t, 8);
CRUCIBLE_SIMD_RAW(std::int32_t, 8);
CRUCIBLE_SIMD_RAW(std::int32_t, 16);
CRUCIBLE_SIMD_RAW(std::uint32_t, 8);
CRUCIBLE_SIMD_RAW(std::uint32_t, 16);
CRUCIBLE_SIMD_RAW(std::uint8_t, 16);
CRUCIBLE_SIMD_RAW(std::uint8_t, 32);
CRUCIBLE_SIMD_RAW(std::int8_t, 16);
CRUCIBLE_SIMD_RAW(std::int8_t, 32);
// The floating-point lanes serve element-wise work and stand as the
// witnesses that the reduction concept rejects.
CRUCIBLE_SIMD_RAW(float, 8);
CRUCIBLE_SIMD_RAW(float, 16);
CRUCIBLE_SIMD_RAW(double, 4);
CRUCIBLE_SIMD_RAW(double, 8);

#undef CRUCIBLE_SIMD_RAW

// A vector comparison yields a signed-integer vector of the same lane
// byte width as its operands.  This maps each element type onto that
// companion type.
template <typename ElementType>
struct mask_element;
template <>
struct mask_element<std::int64_t> {
    using type = std::int64_t;
};
template <>
struct mask_element<std::uint64_t> {
    using type = std::int64_t;
};
template <>
struct mask_element<std::int32_t> {
    using type = std::int32_t;
};
template <>
struct mask_element<std::uint32_t> {
    using type = std::int32_t;
};
template <>
struct mask_element<std::uint8_t> {
    using type = std::int8_t;
};
template <>
struct mask_element<std::int8_t> {
    using type = std::int8_t;
};
template <>
struct mask_element<float> {
    using type = std::int32_t;
};
template <>
struct mask_element<double> {
    using type = std::int64_t;
};

}  // namespace detail

// A lane holds all ones for true and zero for false, which is what the
// comparison produces.  The absence of a value_type member is deliberate:
// it is what keeps a mask out of the reduction concept below.

template <typename MaskElement, int Lanes>
struct mask {
    using raw_type = typename detail::raw_vector<MaskElement, Lanes>::type;
    raw_type m_{};

    constexpr mask() noexcept = default;
    constexpr mask(raw_type raw) noexcept : m_{raw} {}

    [[nodiscard]] static constexpr std::size_t size() noexcept { return static_cast<std::size_t>(Lanes); }
    // The index is int, matching the raw builtin's own subscript type, so
    // no conversion appears on the access.
    [[nodiscard]] constexpr bool operator[](int lane) const noexcept { return m_[lane] != 0; }

    // The logical forms below do not short-circuit.  Both operands are
    // masks that have already been evaluated.
    [[nodiscard]] friend constexpr mask operator&(mask a, mask b) noexcept { return mask{a.m_ & b.m_}; }
    [[nodiscard]] friend constexpr mask operator|(mask a, mask b) noexcept { return mask{a.m_ | b.m_}; }
    [[nodiscard]] friend constexpr mask operator^(mask a, mask b) noexcept { return mask{a.m_ ^ b.m_}; }
    [[nodiscard]] friend constexpr mask operator~(mask a) noexcept { return mask{~a.m_}; }
    [[nodiscard]] friend constexpr mask operator&&(mask a, mask b) noexcept { return mask{a.m_ & b.m_}; }
    [[nodiscard]] friend constexpr mask operator||(mask a, mask b) noexcept { return mask{a.m_ | b.m_}; }
};

template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool any_of(mask<MaskElement, Lanes> m) noexcept {
    bool acc = false;
    for (int lane = 0; lane < Lanes; ++lane)
        acc = acc || (m.m_[lane] != 0);
    return acc;
}
template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool all_of(mask<MaskElement, Lanes> m) noexcept {
    bool acc = true;
    for (int lane = 0; lane < Lanes; ++lane)
        acc = acc && (m.m_[lane] != 0);
    return acc;
}
template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool none_of(mask<MaskElement, Lanes> m) noexcept {
    return !any_of(m);
}

template <typename ElementType, int Lanes>
struct vec {
    using value_type = ElementType;
    using raw_type = typename detail::raw_vector<ElementType, Lanes>::type;
    using mask_type = mask<typename detail::mask_element<ElementType>::type, Lanes>;

    raw_type v_{};

    [[nodiscard]] static constexpr std::size_t size() noexcept { return static_cast<std::size_t>(Lanes); }

    constexpr vec() noexcept = default;
    constexpr vec(raw_type raw) noexcept : v_{raw} {}

    // Every lane-populating constructor brace-initializes the raw vector
    // through a pack expansion instead of assigning lane by lane.  A
    // subscript write on a vector builtin is not a constant expression on
    // this compiler, which would put the constexpr factories below out of
    // reach during constant evaluation.

    explicit constexpr vec(ElementType scalar) noexcept
        : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) noexcept -> raw_type {
              return raw_type{((void)Lane, scalar)...};
          }(std::make_integer_sequence<int, Lanes>{})) {}

    // The constraint is what keeps a scalar argument out of this
    // overload, leaving it to the broadcast constructor above.
    template <typename Generator>
        requires std::is_invocable_v<Generator&&, std::integral_constant<int, 0>>
    explicit constexpr vec(Generator&& gen) noexcept
        : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) -> raw_type {
              return raw_type{static_cast<ElementType>(gen(std::integral_constant<int, Lane>{}))...};
          }(std::make_integer_sequence<int, Lanes>{})) {}

    template <typename OtherElement>
        requires(!std::is_same_v<OtherElement, ElementType>)
    explicit constexpr vec(vec<OtherElement, Lanes> other) noexcept
        : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) noexcept -> raw_type {
              return raw_type{static_cast<ElementType>(other.v_[Lane])...};
          }(std::make_integer_sequence<int, Lanes>{})) {}

    [[nodiscard]] constexpr ElementType operator[](int lane) const noexcept { return v_[lane]; }

    [[nodiscard]] friend constexpr vec operator+(vec a, vec b) noexcept { return vec{a.v_ + b.v_}; }
    [[nodiscard]] friend constexpr vec operator-(vec a, vec b) noexcept { return vec{a.v_ - b.v_}; }
    [[nodiscard]] friend constexpr vec operator*(vec a, vec b) noexcept { return vec{a.v_ * b.v_}; }
    [[nodiscard]] friend constexpr vec operator&(vec a, vec b) noexcept { return vec{a.v_ & b.v_}; }
    [[nodiscard]] friend constexpr vec operator|(vec a, vec b) noexcept { return vec{a.v_ | b.v_}; }
    [[nodiscard]] friend constexpr vec operator^(vec a, vec b) noexcept { return vec{a.v_ ^ b.v_}; }
    [[nodiscard]] friend constexpr vec operator~(vec a) noexcept { return vec{~a.v_}; }
    [[nodiscard]] friend constexpr vec operator-(vec a) noexcept { return vec{-a.v_}; }
    [[nodiscard]] friend constexpr vec operator<<(vec a, int s) noexcept { return vec{a.v_ << s}; }
    [[nodiscard]] friend constexpr vec operator>>(vec a, int s) noexcept { return vec{a.v_ >> s}; }
    [[nodiscard]] friend constexpr vec operator<<(vec a, vec s) noexcept { return vec{a.v_ << s.v_}; }
    [[nodiscard]] friend constexpr vec operator>>(vec a, vec s) noexcept { return vec{a.v_ >> s.v_}; }

    [[nodiscard]] friend constexpr mask_type operator==(vec a, vec b) noexcept { return mask_type{a.v_ == b.v_}; }
    [[nodiscard]] friend constexpr mask_type operator!=(vec a, vec b) noexcept { return mask_type{a.v_ != b.v_}; }
    [[nodiscard]] friend constexpr mask_type operator<(vec a, vec b) noexcept { return mask_type{a.v_ < b.v_}; }
    [[nodiscard]] friend constexpr mask_type operator<=(vec a, vec b) noexcept { return mask_type{a.v_ <= b.v_}; }
    [[nodiscard]] friend constexpr mask_type operator>(vec a, vec b) noexcept { return mask_type{a.v_ > b.v_}; }
    [[nodiscard]] friend constexpr mask_type operator>=(vec a, vec b) noexcept { return mask_type{a.v_ >= b.v_}; }
};

using i64x4 = vec<std::int64_t, 4>;
using i64x8 = vec<std::int64_t, 8>;
using u64x4 = vec<std::uint64_t, 4>;
using u64x8 = vec<std::uint64_t, 8>;
using i32x8 = vec<std::int32_t, 8>;
using i32x16 = vec<std::int32_t, 16>;
using u32x8 = vec<std::uint32_t, 8>;
using u32x16 = vec<std::uint32_t, 16>;
using u8x16 = vec<std::uint8_t, 16>;
using u8x32 = vec<std::uint8_t, 32>;

using i64x8_mask = i64x8::mask_type;
using u64x8_mask = u64x8::mask_type;
using u32x8_mask = u32x8::mask_type;

template <typename V>
concept DetSafeSimd = requires { typename V::value_type; } && std::integral<typename V::value_type>;

// The copy is what keeps the load and store free of an aliasing
// violation.  Reading the element array through a vector-typed pointer
// would not be.

template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V load(const typename V::value_type* ptr) noexcept {
    typename V::raw_type raw;
    std::memcpy(&raw, ptr, sizeof(raw));
    return V{raw};
}

template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V load_aligned(const typename V::value_type* ptr) noexcept {
    const auto* aligned =
        static_cast<const typename V::value_type*>(__builtin_assume_aligned(ptr, alignof(typename V::raw_type)));
    typename V::raw_type raw;
    std::memcpy(&raw, aligned, sizeof(raw));
    return V{raw};
}

// The caller owes count no greater than the lane count.  Lanes past
// count stay zero.
template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V partial_load(const typename V::value_type* ptr, int count) noexcept {
    V result{};
    for (int lane = 0; lane < count; ++lane)
        result.v_[lane] = ptr[lane];
    return result;
}

template <typename V>
CRUCIBLE_INLINE void store(V value, typename V::value_type* ptr) noexcept {
    std::memcpy(ptr, &value.v_, sizeof(value.v_));
}

template <typename V>
CRUCIBLE_INLINE void store_aligned(V value, typename V::value_type* ptr) noexcept {
    auto* aligned = static_cast<typename V::value_type*>(__builtin_assume_aligned(ptr, alignof(typename V::raw_type)));
    std::memcpy(aligned, &value.v_, sizeof(value.v_));
}

// The mask has the same lane byte width as the value, so its bits cast
// straight onto the value type and the blend is a pair of bitwise
// operations with no branch.

template <typename ElementType, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE vec<ElementType, Lanes> select(typename vec<ElementType, Lanes>::mask_type selector,
                                                             vec<ElementType, Lanes> on_true,
                                                             vec<ElementType, Lanes> on_false) noexcept {
    using RawType = typename vec<ElementType, Lanes>::raw_type;
    const RawType bits = std::bit_cast<RawType>(selector.m_);
    return vec<ElementType, Lanes>{(on_true.v_ & bits) | (on_false.v_ & ~bits)};
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_xor(V value) noexcept {
    typename V::value_type acc = value.v_[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc ^ value.v_[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_add(V value) noexcept {
    typename V::value_type acc = value.v_[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc + value.v_[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_and(V value) noexcept {
    typename V::value_type acc = value.v_[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc & value.v_[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_or(V value) noexcept {
    typename V::value_type acc = value.v_[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc | value.v_[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_min(V value) noexcept {
    typename V::value_type acc = value.v_[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = value.v_[lane] < acc ? value.v_[lane] : acc;
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_max(V value) noexcept {
    typename V::value_type acc = value.v_[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = value.v_[lane] > acc ? value.v_[lane] : acc;
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_xor(V value, typename V::mask_type valid) noexcept {
    using ElementType = typename V::value_type;
    constexpr int Lanes = static_cast<int>(V::size());
    return reduce_xor(select<ElementType, Lanes>(valid, value, V{static_cast<ElementType>(0)}));
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_add(V value, typename V::mask_type valid) noexcept {
    using ElementType = typename V::value_type;
    constexpr int Lanes = static_cast<int>(V::size());
    return reduce_add(select<ElementType, Lanes>(valid, value, V{static_cast<ElementType>(0)}));
}

template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V min(V a, V b) noexcept {
    return select<typename V::value_type, static_cast<int>(V::size())>(a < b, a, b);
}
template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V max(V a, V b) noexcept {
    return select<typename V::value_type, static_cast<int>(V::size())>(a > b, a, b);
}

template <typename V>
[[nodiscard, gnu::const]] constexpr V iota_v() noexcept {
    using ElementType = typename V::value_type;
    return V([](auto lane) noexcept -> ElementType { return static_cast<ElementType>(decltype(lane)::value); });
}

template <typename V>
[[nodiscard, gnu::const]] CRUCIBLE_INLINE typename V::mask_type prefix_mask(int count) noexcept {
    using ElementType = typename V::value_type;
    return iota_v<V>() < V(static_cast<ElementType>(count));
}

#if defined(__AVX512F__)
inline constexpr bool kAvx512Available = true;
#else
inline constexpr bool kAvx512Available = false;
#endif

#if defined(__AVX2__)
inline constexpr bool kAvx2Available = true;
#else
inline constexpr bool kAvx2Available = false;
#endif

#if defined(__SSE4_2__)
inline constexpr bool kSse42Available = true;
#else
inline constexpr bool kSse42Available = false;
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
inline constexpr bool kNeonAvailable = true;
#else
inline constexpr bool kNeonAvailable = false;
#endif

#if defined(__x86_64__) || defined(__i386__)
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx512() noexcept { return __builtin_cpu_supports("avx512f"); }
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx2() noexcept { return __builtin_cpu_supports("avx2"); }
[[nodiscard, gnu::pure]] inline bool runtime_supports_sse42() noexcept { return __builtin_cpu_supports("sse4.2"); }
#else
[[nodiscard]] constexpr bool runtime_supports_avx512() noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_avx2() noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_sse42() noexcept { return false; }
#endif

}  // namespace crucible::simd
