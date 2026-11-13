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

#include <foundation/Platform.h>
#include <foundation/core/Region.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace foundation::simd {

namespace detail {

// The element types that GCC accepts in a vector: an arithmetic type
// other than bool, without cv qualification.  The lane count is a power
// of two, and a vector of one lane is a scalar, so the least is two.
template <typename ElementType, int Lanes>
concept VectorShape = std::is_arithmetic_v<ElementType> && !std::is_same_v<ElementType, bool>
                   && std::is_same_v<ElementType, std::remove_cv_t<ElementType>> && Lanes >= 2
                   && std::has_single_bit(static_cast<unsigned>(Lanes));

// GCC keeps the vector-size attribute on the declaration of a member
// alias with a dependent element type, so one primary template gives
// every element-and-lane pair.  Written after the type, as in
// `using V = E __attribute__((vector_size(N)))`, the attribute has no
// declaration to attach to, and GCC drops it with a warning.
template <typename ElementType, int Lanes>
    requires VectorShape<ElementType, Lanes>
struct raw_vector {
    using type [[gnu::vector_size(static_cast<std::size_t>(Lanes) * sizeof(ElementType))]] = ElementType;
};

template <typename ElementType, int Lanes>
using raw_vector_t = typename raw_vector<ElementType, Lanes>::type;

// A vector comparison yields a signed-integer vector of the lane byte
// width of its operands.  The lane type of a mask is the element of that
// result, read from the comparison itself.
template <typename ElementType, int Lanes>
using comparison_t =
    decltype(std::declval<raw_vector_t<ElementType, Lanes>>() == std::declval<raw_vector_t<ElementType, Lanes>>());

template <typename ElementType, int Lanes>
using comparison_lane_t = std::remove_cvref_t<decltype(std::declval<comparison_t<ElementType, Lanes>>()[0])>;

}  // namespace detail

// A lane holds all ones for true and zero for false, which is what the
// comparison produces.  The absence of a value_type member is deliberate:
// it is what keeps a mask out of the reduction concept below.

template <typename MaskElement, int Lanes>
    requires std::signed_integral<MaskElement> && detail::VectorShape<MaskElement, Lanes>
class mask {
public:
    using raw_type = detail::raw_vector_t<MaskElement, Lanes>;

    constexpr mask() noexcept = default;
    constexpr mask(raw_type raw) noexcept : m_{raw} {}

    [[nodiscard]] static constexpr std::size_t size() noexcept { return static_cast<std::size_t>(Lanes); }
    // The raw vector, by value, for the free operations of the facade and for
    // the builtins of a SIMD arm.
    [[nodiscard]] constexpr raw_type raw() const noexcept { return m_; }
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

private:
    raw_type m_{};
};

template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool any_of(mask<MaskElement, Lanes> m) noexcept {
    const auto lanes = m.raw();
    bool acc = false;
    for (int lane = 0; lane < Lanes; ++lane)
        acc = acc || (lanes[lane] != 0);
    return acc;
}
template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool all_of(mask<MaskElement, Lanes> m) noexcept {
    const auto lanes = m.raw();
    bool acc = true;
    for (int lane = 0; lane < Lanes; ++lane)
        acc = acc && (lanes[lane] != 0);
    return acc;
}
template <typename MaskElement, int Lanes>
[[nodiscard]] constexpr bool none_of(mask<MaskElement, Lanes> m) noexcept {
    return !any_of(m);
}

template <typename ElementType, int Lanes>
    requires detail::VectorShape<ElementType, Lanes>
class vec {
public:
    using value_type = ElementType;
    using raw_type = detail::raw_vector_t<ElementType, Lanes>;
    using mask_type = mask<detail::comparison_lane_t<ElementType, Lanes>, Lanes>;

    [[nodiscard]] static constexpr std::size_t size() noexcept { return static_cast<std::size_t>(Lanes); }
    // The raw vector, by value, for the free operations of the facade and for
    // the builtins of a SIMD arm.
    [[nodiscard]] constexpr raw_type raw() const noexcept { return v_; }

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

private:
    // The converting constructor reads each lane of a vec of a different
    // element type in place, with no copy of the source vector.
    template <typename OtherElement, int OtherLanes>
        requires detail::VectorShape<OtherElement, OtherLanes>
    friend class vec;

    raw_type v_{};
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

// A View of the bytes of one register of V: an element of one byte that a
// copy can read, and exactly as many elements as the register has bytes.
template <typename V, typename Byte, std::size_t Bytes>
concept RegisterBytes = requires { typename V::raw_type; } && sizeof(Byte) == 1
                     && std::is_trivially_copyable_v<Byte> && Bytes == sizeof(typename V::raw_type);

// The load of a vector from the bytes of one register, the lowest lane
// first.  The type of the View proves the count, so the copy has no check,
// and the bytes need no alignment.
template <typename V, typename Byte, std::size_t Bytes>
    requires RegisterBytes<V, Byte, Bytes>
[[nodiscard]] CRUCIBLE_INLINE V load(core::View<Byte, Bytes> bytes) noexcept {
    typename V::raw_type raw{};
    __builtin_memcpy(&raw, first_of_(core::detail::ViewDoor::key(), bytes), sizeof(raw));
    return V{raw};
}

template <typename V>
CRUCIBLE_INLINE void store(V value, typename V::value_type* ptr) noexcept {
    const typename V::raw_type raw = value.raw();
    std::memcpy(ptr, &raw, sizeof(raw));
}

template <typename V>
CRUCIBLE_INLINE void store_aligned(V value, typename V::value_type* ptr) noexcept {
    auto* aligned = static_cast<typename V::value_type*>(__builtin_assume_aligned(ptr, alignof(typename V::raw_type)));
    const typename V::raw_type raw = value.raw();
    std::memcpy(aligned, &raw, sizeof(raw));
}

// The store of a vector into the bytes of one register, the lowest lane
// first.  The type of the View proves the count, so the copy has no check,
// and the bytes need no alignment.
template <typename V, typename Byte, std::size_t Bytes>
    requires RegisterBytes<V, Byte, Bytes> && (!std::is_const_v<Byte>)
CRUCIBLE_INLINE void store(V value, core::View<Byte, Bytes> bytes) noexcept {
    const typename V::raw_type raw = value.raw();
    __builtin_memcpy(first_of_(core::detail::ViewDoor::key(), bytes), &raw, sizeof(raw));
}

// The bits of one register as a vector of a different lane type, the view
// of the register that a builtin of a SIMD arm takes.  No bit changes.
template <typename To, typename From>
    requires requires {
        typename To::raw_type;
        typename From::raw_type;
    } && (sizeof(typename To::raw_type) == sizeof(typename From::raw_type))
[[nodiscard]] constexpr To bit_cast(From value) noexcept {
    return To{__builtin_bit_cast(typename To::raw_type, value.raw())};
}

// A vec or a mask of one-byte lanes that fills one register of 16, 32 or 64
// bytes.  Each lane gives one bit of a 64-bit word.
template <typename V>
concept ByteLaneRegister =
    requires { typename V::raw_type; } && sizeof(std::declval<typename V::raw_type const&>()[0]) == 1
    && (sizeof(typename V::raw_type) == 16 || sizeof(typename V::raw_type) == 32 || sizeof(typename V::raw_type) == 64);

namespace detail {

// Bit 7 of each byte of word, the byte at offset i at bit i of the result.
// The and keeps bit 8i + 7 of each byte.  The multiplier has a bit at each
// multiple of 7 from 0 to 49, so bit 8i + 7 times bit 49 - 7i lands at bit
// 56 + i.  No two partial products share a bit, so no carry reaches the top
// byte.
[[nodiscard]] constexpr std::uint64_t collect_word_sign_bits(std::uint64_t word) noexcept {
    return ((word & 0x8080808080808080u) * 0x0002040810204081u) >> 56;
}

// The sign bit of each lane with one multiply for each 8 lanes: the form of
// a target that has no instruction for the width.
template <int Lanes>
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t collect_sign_bits_by_word(raw_vector_t<char, Lanes> lanes) noexcept {
    using Words = raw_vector_t<std::uint64_t, Lanes / 8>;
    Words const words = __builtin_bit_cast(Words, lanes);
    std::uint64_t bits = 0;
    for (int word = 0; word < Lanes / 8; ++word)
        bits |= collect_word_sign_bits(words[word]) << (8 * word);
    return bits;
}

}  // namespace detail

// The sign bit of each lane of value, lane i at bit i of the result, and
// zero above the last lane.  A lane of a mask is all ones or zero, so its
// bit is its truth.  On x86 it is one instruction: vpmovb2m for 64 lanes
// with AVX-512BW, vpmovmskb for 32 lanes with AVX2, and pmovmskb for 16
// lanes.  Each other target, and a width with no instruction, takes one
// multiply for each 8 lanes.
template <typename V>
    requires ByteLaneRegister<V>
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t collect_sign_bits(V value) noexcept {
    constexpr int lanes = static_cast<int>(sizeof(typename V::raw_type));
    using Chars = detail::raw_vector_t<char, lanes>;
    Chars const chars = __builtin_bit_cast(Chars, value.raw());
#if defined(__AVX512BW__)
    if constexpr (lanes == 64) {
        return __builtin_ia32_cvtb2mask512(chars);
    }
#endif
#if defined(__AVX2__)
    // The movemask gives an int.  The conversion through uint32_t keeps a
    // set bit 31 out of the upper half of the result.
    if constexpr (lanes == 32) {
        return static_cast<std::uint32_t>(__builtin_ia32_pmovmskb256(chars));
    }
#endif
#if defined(__SSE2__)
    if constexpr (lanes == 16) {
        return static_cast<std::uint32_t>(__builtin_ia32_pmovmskb128(chars));
    }
#endif
    return detail::collect_sign_bits_by_word<lanes>(chars);
}

// Bit i set when lane i of left and lane i of right are equal, and zero
// above the last lane.  With AVX-512BW, 64 lanes compare into a mask
// register with one instruction.  GCC does not fold a compare into a vector
// and a sign gather into that instruction, so this arm calls it directly.
// Each other width and target compares into a vector and takes its signs.
template <typename ElementType, int Lanes>
    requires ByteLaneRegister<vec<ElementType, Lanes>>
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t collect_equal_bits(vec<ElementType, Lanes> left,
                                                               vec<ElementType, Lanes> right) noexcept {
#if defined(__AVX512BW__)
    if constexpr (Lanes == 64) {
        using Chars = detail::raw_vector_t<char, 64>;
        return __builtin_ia32_pcmpeqb512_mask(__builtin_bit_cast(Chars, left.raw()),
                                              __builtin_bit_cast(Chars, right.raw()), ~0ULL);
    }
#endif
    return collect_sign_bits(left == right);
}

// A body that takes the Views of one register run of each of two Views and
// does not throw.
template <typename Body, typename V, typename First, typename Second>
concept RegisterRunBody = std::is_nothrow_invocable_v<Body&, core::View<First, sizeof(typename V::raw_type)>,
                                                      core::View<Second, sizeof(typename V::raw_type)>>;

// Calls body with the Views of each run of the bytes of one register of first
// and of second, in lockstep, the lowest run first, and gives the two Views of
// the bytes after the last whole run.  A SIMD kernel then reads its target and
// its source with one loop and no check in it.  Two counts that differ end
// the process before the loop, and the optimizer removes that compare for two
// Views of one count.  Complexity: one call of body for each whole run.
template <typename V, typename First, typename Second, typename Body>
    requires RegisterBytes<V, First, sizeof(typename V::raw_type)>
          && RegisterBytes<V, Second, sizeof(typename V::raw_type)> && RegisterRunBody<Body, V, First, Second>
CRUCIBLE_INLINE core::Record<core::View<First>, core::View<Second>>
for_each_register_run(core::View<First> first, core::View<Second> second, Body body) noexcept {
    constexpr std::size_t width = sizeof(typename V::raw_type);
    if (first.size() != second.size()) [[unlikely]] {
        core::fatal("the register runs of two Views of {} and {} elements", first.size(), second.size());
    }
    First* const first_at = first_of_(core::detail::ViewDoor::key(), first);
    Second* const second_at = first_of_(core::detail::ViewDoor::key(), second);
    // The loop counts whole runs, so the optimizer and -Warray-bounds read
    // each offset as below the count of the runs.
    std::size_t const runs = first.size() / width;
    for (std::size_t run = 0; run < runs; ++run) {
        body(core::View<First, width>{core::detail::ViewDoor::key(), first_at + run * width},
             core::View<Second, width>{core::detail::ViewDoor::key(), second_at + run * width});
    }
    // The rest is the count modulo the width, so the optimizer reads it as
    // below one register.
    std::size_t const whole = runs * width;
    std::size_t const rest = first.size() % width;
    return {core::View<First>{core::detail::ViewDoor::key(), first_at + whole, rest},
            core::View<Second>{core::detail::ViewDoor::key(), second_at + whole, rest}};
}

// The mask has the same lane byte width as the value, so its bits cast
// straight onto the value type and the blend is a pair of bitwise
// operations with no branch.

template <typename ElementType, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE vec<ElementType, Lanes> select(typename vec<ElementType, Lanes>::mask_type selector,
                                                             vec<ElementType, Lanes> on_true,
                                                             vec<ElementType, Lanes> on_false) noexcept {
    using RawType = typename vec<ElementType, Lanes>::raw_type;
    const RawType bits = std::bit_cast<RawType>(selector.raw());
    return vec<ElementType, Lanes>{(on_true.raw() & bits) | (on_false.raw() & ~bits)};
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_xor(V value) noexcept {
    const typename V::raw_type lanes = value.raw();
    typename V::value_type acc = lanes[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc ^ lanes[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_add(V value) noexcept {
    const typename V::raw_type lanes = value.raw();
    typename V::value_type acc = lanes[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc + lanes[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_and(V value) noexcept {
    const typename V::raw_type lanes = value.raw();
    typename V::value_type acc = lanes[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc & lanes[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_or(V value) noexcept {
    const typename V::raw_type lanes = value.raw();
    typename V::value_type acc = lanes[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<typename V::value_type>(acc | lanes[lane]);
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_min(V value) noexcept {
    const typename V::raw_type lanes = value.raw();
    typename V::value_type acc = lanes[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = lanes[lane] < acc ? lanes[lane] : acc;
    return acc;
}

template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_max(V value) noexcept {
    const typename V::raw_type lanes = value.raw();
    typename V::value_type acc = lanes[0];
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = lanes[lane] > acc ? lanes[lane] : acc;
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

}  // namespace foundation::simd
