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
//
// The facade is in the inline namespace CRUCIBLE_SIMD_TARGET (the target
// namespace of foundation/Platform.h), so each target has its own vec, its
// own mask and its own body of each operation.  The rule for new code:
//   - An inline function or template of the base whose body uses the facade,
//     or reads a target macro or a constant such as native_lanes, is in the
//     inline namespace CRUCIBLE_SIMD_TARGET of its own namespace.  So is each
//     inline function or template of the base whose body calls such a
//     function.  A type, a concept and a function with no such body stay
//     outside it.
//   - In a namespace that also holds names outside the inline namespace, open
//     the inline namespace as the innermost namespace, as in
//     `namespace detail { inline namespace CRUCIBLE_SIMD_TARGET {`.  A
//     namespace detail inside the inline namespace hides the outer detail
//     from the functions in it.  This header holds each name of
//     foundation::simd in the inline namespace, so its detail is inside.
//   - Declare no name of this facade outside this header.  A declaration of
//     foundation::simd::vec outside the inline namespace declares a second
//     template.

#include <foundation/Platform.h>
#include <foundation/core/Number.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace foundation::simd {
inline namespace CRUCIBLE_SIMD_TARGET {

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

// The vector that a lane operation computes in.  A signed integer lane
// computes in its unsigned type, where an add, a subtract, a multiply and a
// negate wrap modulo two to the width of the lane.  In the signed type, a
// result outside the range is an overflow, and a signed overflow is undefined
// behavior.  Each other lane computes in its own type.
template <typename ElementType, int Lanes>
struct wrap_vector {
    using type = raw_vector_t<ElementType, Lanes>;
};

template <typename ElementType, int Lanes>
    requires std::signed_integral<ElementType>
struct wrap_vector<ElementType, Lanes> {
    using type = raw_vector_t<std::make_unsigned_t<ElementType>, Lanes>;
};

template <typename ElementType, int Lanes>
using wrap_vector_t = typename wrap_vector<ElementType, Lanes>::type;

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
    // no conversion appears on the access.  A lane outside the mask ends the
    // process in each build, because the builtin subscript reads past the
    // vector.  The optimizer folds the test for a constant lane and for the
    // lane of a loop below the count.
    [[nodiscard]] constexpr bool operator[](int lane) const noexcept {
        if (lane < 0 || lane >= Lanes) [[unlikely]] {
            ::foundation::core::fatal("a read of lane {} of a mask of {} lanes", lane, Lanes);
        }
        return m_[lane] != 0;
    }

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
    // overload, leaving it to the broadcast constructor above.  The
    // generator holds no linear value by value (HoldsNoLinearValue of
    // foundation/Linear.h): the constructor calls it for each lane and ends
    // it, and a call is no consume.
    template <typename Generator>
        requires std::is_invocable_v<Generator&&, std::integral_constant<int, 0>>
              && ::foundation::linear::HoldsNoLinearValue<Generator>
    explicit constexpr vec(Generator&& gen) noexcept
        : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) -> raw_type {
              return raw_type{static_cast<ElementType>(gen(std::integral_constant<int, Lane>{}))...};
          }(std::make_integer_sequence<int, Lanes>{})) {}

    // A generator that holds a linear value by value binds to this overload,
    // so the refusal gives its reason.  The parse kit reads no macro in the
    // reason of a deleted constructor, so the reason is the text of
    // CRUCIBLE_LINEAR_BODY_REFUSAL.
    template <typename Generator>
        requires std::is_invocable_v<Generator&&, std::integral_constant<int, 0>>
                  && (!::foundation::linear::HoldsNoLinearValue<Generator>)
    explicit vec(Generator&& gen) =
        delete("the body holds a linear value by value, and the door calls it with no consume: keep the linear value "
               "a local of the caller, and let the body capture it by reference or take it as a parameter");

    template <typename OtherElement>
        requires(!std::is_same_v<OtherElement, ElementType>)
    explicit constexpr vec(vec<OtherElement, Lanes> other) noexcept
        : v_([&]<int... Lane>(std::integer_sequence<int, Lane...>) noexcept -> raw_type {
              return raw_type{static_cast<ElementType>(other.v_[Lane])...};
          }(std::make_integer_sequence<int, Lanes>{})) {}

    // A lane outside the vector ends the process in each build, as the
    // subscript of a mask does.
    [[nodiscard]] constexpr ElementType operator[](int lane) const noexcept {
        if (lane < 0 || lane >= Lanes) [[unlikely]] {
            ::foundation::core::fatal("a read of lane {} of a vector of {} lanes", lane, Lanes);
        }
        return v_[lane];
    }

    // On signed integer lanes, the add, the subtract, the multiply and the
    // negate wrap modulo two to the width of the lane, with no overflow
    // (detail::wrap_vector).  The bits of each lane and the instructions are
    // the same as in the signed type.
    [[nodiscard]] friend constexpr vec operator+(vec a, vec b) noexcept { return unwrap_(wrap_(a) + wrap_(b)); }
    [[nodiscard]] friend constexpr vec operator-(vec a, vec b) noexcept { return unwrap_(wrap_(a) - wrap_(b)); }
    [[nodiscard]] friend constexpr vec operator*(vec a, vec b) noexcept { return unwrap_(wrap_(a) * wrap_(b)); }
    // The IEEE division of each lane of a floating vector.  An integer lane
    // has no division: a lane of zero would trap.
    [[nodiscard]] friend constexpr vec operator/(vec a, vec b) noexcept
        requires std::floating_point<ElementType>
    {
        return vec{a.v_ / b.v_};
    }
    [[nodiscard]] friend constexpr vec operator&(vec a, vec b) noexcept { return vec{a.v_ & b.v_}; }
    [[nodiscard]] friend constexpr vec operator|(vec a, vec b) noexcept { return vec{a.v_ | b.v_}; }
    [[nodiscard]] friend constexpr vec operator^(vec a, vec b) noexcept { return vec{a.v_ ^ b.v_}; }
    [[nodiscard]] friend constexpr vec operator~(vec a) noexcept { return vec{~a.v_}; }
    [[nodiscard]] friend constexpr vec operator-(vec a) noexcept { return unwrap_(-wrap_(a)); }
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

    using wrap_type = detail::wrap_vector_t<ElementType, Lanes>;

    // The bits of the lanes in the vector of the wrapping operations, and
    // back.  No bit changes.
    [[nodiscard]] static constexpr wrap_type wrap_(vec value) noexcept {
        return __builtin_bit_cast(wrap_type, value.v_);
    }
    [[nodiscard]] static constexpr vec unwrap_(wrap_type value) noexcept {
        return vec{__builtin_bit_cast(raw_type, value)};
    }

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
    __builtin_memcpy(&raw, first_of_(core::detail::BaseLayerRead{}, bytes), sizeof(raw));
    return V{raw};
}

// A View of the lanes of one register of V: elements of the lane type of V
// wider than one byte, and exactly as many as V has lanes.  A View of bytes
// takes the load of RegisterBytes.
template <typename V, typename Lane, std::size_t Lanes>
concept RegisterLanes =
    requires {
        typename V::raw_type;
        typename V::value_type;
    } && std::is_same_v<std::remove_const_t<Lane>, typename V::value_type> && (sizeof(Lane) > 1)
    && Lanes * sizeof(Lane) == sizeof(typename V::raw_type);

// The load of a vector from the lanes of one register, the lowest lane
// first.  The type of the View proves the count, so the copy has no check,
// and the lanes need no alignment beyond the alignment of their type.
template <typename V, typename Lane, std::size_t Lanes>
    requires RegisterLanes<V, Lane, Lanes>
[[nodiscard]] CRUCIBLE_INLINE V load(core::View<Lane, Lanes> lanes) noexcept {
    typename V::raw_type raw{};
    __builtin_memcpy(&raw, first_of_(core::detail::BaseLayerRead{}, lanes), sizeof(raw));
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

// The lanes of one register of V that a store can write: the lanes of
// RegisterLanes, and no lane is const.
template <typename V, typename Lane, std::size_t Lanes>
concept WritableRegisterLanes = RegisterLanes<V, Lane, Lanes> && !std::is_const_v<Lane>;

// The store of a vector into the bytes or the lanes of one register of the
// run of a frame, the lowest lane first.  The store is the mirror of the
// load.  The door takes the ViewMut of the frame by value, as fill does, so
// the store is the one writer of the run.  The type of the ViewMut proves the
// count, so the copy has no check.  The bytes need no alignment, and the
// lanes need no alignment beyond the alignment of their type.
template <typename V, typename Lane, ::foundation::brand::IsBrand Brand, std::size_t Lanes>
    requires RegisterBytes<V, Lane, Lanes> || WritableRegisterLanes<V, Lane, Lanes>
CRUCIBLE_INLINE void store(V value, core::ViewMut<Lane, Brand, Lanes> lanes) noexcept {
    const typename V::raw_type raw = value.raw();
    __builtin_memcpy(first_of_(core::detail::BaseLayerRead{}, lanes), &raw, sizeof(raw));
}

namespace detail {

// The mask of the first bytes of a register of 64, 32 or 16 bytes: bit i set
// for each byte i below bytes.
[[nodiscard]] constexpr std::uint64_t prefix_byte_mask(std::size_t bytes) noexcept {
    return bytes >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bytes) - 1;
}

}  // namespace detail

// True when load_prefix and store_prefix take one masked load or store of
// bytes, and false when they copy the bytes through memory.  A copy of a
// run-time size is much slower than a full load, so a loop can choose a full
// load when it knows that the bytes past the prefix are inside its run.
#if defined(__AVX512BW__) && defined(__AVX512VL__)
inline constexpr bool kPrefixLoadIsMasked = true;
#else
inline constexpr bool kPrefixLoadIsMasked = false;
#endif

// The first count lanes of a vector from ptr, and zero in each lane at or
// past count.  The load reads the count elements and no other byte, so a run
// that ends at the last of them is safe to read.  With AVX-512BW, a register
// of 16, 32 or 64 bytes takes one masked load of bytes, which suppresses the
// fault of each byte past the mask.  Each other target copies the bytes into
// a zeroed register.  A count past the lane count ends the process in each
// build.
template <typename V>
[[nodiscard]] CRUCIBLE_INLINE V load_prefix(const typename V::value_type* ptr, std::size_t count) noexcept {
    using Raw = typename V::raw_type;
    constexpr std::size_t lane_bytes = sizeof(typename V::value_type);
    if (count > V::size()) [[unlikely]] {
        ::foundation::core::fatal("a prefix load of {} lanes into a vector of {} lanes", count, V::size());
    }
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    char const* const bytes = static_cast<char const*>(static_cast<void const*>(ptr));
    std::uint64_t const mask = detail::prefix_byte_mask(count * lane_bytes);
    if constexpr (sizeof(Raw) == 64) {
        using Chars = detail::raw_vector_t<char, 64>;
        return V{__builtin_bit_cast(Raw, __builtin_ia32_loaddquqi512_mask(bytes, Chars{}, mask))};
    } else if constexpr (sizeof(Raw) == 32) {
        using Chars = detail::raw_vector_t<char, 32>;
        return V{__builtin_bit_cast(
            Raw, __builtin_ia32_loaddquqi256_mask(bytes, Chars{}, static_cast<std::uint32_t>(mask)))};
    } else if constexpr (sizeof(Raw) == 16) {
        using Chars = detail::raw_vector_t<char, 16>;
        return V{__builtin_bit_cast(
            Raw, __builtin_ia32_loaddquqi128_mask(bytes, Chars{}, static_cast<std::uint16_t>(mask)))};
    }
#endif
    Raw raw{};
    __builtin_memcpy(&raw, ptr, count * lane_bytes);
    return V{raw};
}

// Writes the first count lanes of value to ptr, and no other byte.  The arms
// are the arms of load_prefix.  A count past the lane count ends the process
// in each build.
template <typename V>
CRUCIBLE_INLINE void store_prefix(V value, typename V::value_type* ptr, std::size_t count) noexcept {
    using Raw = typename V::raw_type;
    constexpr std::size_t lane_bytes = sizeof(typename V::value_type);
    if (count > V::size()) [[unlikely]] {
        ::foundation::core::fatal("a prefix store of {} lanes from a vector of {} lanes", count, V::size());
    }
    Raw const raw = value.raw();
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    char* const bytes = static_cast<char*>(static_cast<void*>(ptr));
    std::uint64_t const mask = detail::prefix_byte_mask(count * lane_bytes);
    if constexpr (sizeof(Raw) == 64) {
        __builtin_ia32_storedquqi512_mask(bytes, __builtin_bit_cast(detail::raw_vector_t<char, 64>, raw), mask);
        return;
    } else if constexpr (sizeof(Raw) == 32) {
        __builtin_ia32_storedquqi256_mask(bytes, __builtin_bit_cast(detail::raw_vector_t<char, 32>, raw),
                                          static_cast<std::uint32_t>(mask));
        return;
    } else if constexpr (sizeof(Raw) == 16) {
        __builtin_ia32_storedquqi128_mask(bytes, __builtin_bit_cast(detail::raw_vector_t<char, 16>, raw),
                                          static_cast<std::uint16_t>(mask));
        return;
    }
#endif
    __builtin_memcpy(ptr, &raw, count * lane_bytes);
}

// The prefix load of the lanes of a View: the first lanes.size() lanes, and
// zero in each other lane.  A View of more lanes than V holds ends the
// process in each build.
template <typename V, typename Lane>
    requires std::is_same_v<std::remove_const_t<Lane>, typename V::value_type>
[[nodiscard]] CRUCIBLE_INLINE V load_prefix(core::View<Lane> lanes) noexcept {
    return load_prefix<V>(first_of_(core::detail::BaseLayerRead{}, lanes), lanes.size());
}

// The prefix store into the run of a frame: the first lanes.size() lanes of
// value, and no other byte.  The door takes the ViewMut of the frame by
// value, as fill does, so the store is the one writer of the run.  A run of
// more lanes than V holds ends the process in each build.
template <typename V, typename Lane, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires std::is_same_v<Lane, typename V::value_type>
CRUCIBLE_INLINE void store_prefix(V value, core::ViewMut<Lane, Brand, Extent> lanes) noexcept {
    store_prefix(value, first_of_(core::detail::BaseLayerRead{}, lanes), lanes.size());
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

// The lane counts of a lookup in a table of 16 bytes: one register of 16,
// 32 or 64 byte lanes.
template <int Lanes>
concept NibbleLookupLanes = Lanes == 16 || Lanes == 32 || Lanes == 64;

namespace detail {

// The numbers of the lanes of a register, from 0 to Lanes - 1, as a pack.
// __integer_pack of GCC gives the pack, as it does for the integer sequence
// of libstdc++.
template <int... Lane>
struct LaneList {};

template <int Lanes>
using lane_list_t = LaneList<__integer_pack(Lanes)...>;

// The table of 16 bytes in each run of 16 lanes: lane i holds entry i % 16.
// For 16 lanes the shuffle is the identity, and it emits nothing.
template <int Lanes, int... Lane>
[[nodiscard]] CRUCIBLE_INLINE raw_vector_t<std::uint8_t, Lanes>
repeat_nibble_table(raw_vector_t<std::uint8_t, 16> table, LaneList<Lane...> /*lanes*/) noexcept {
    return __builtin_shufflevector(table, table, (Lane % 16)...);
}

}  // namespace detail

// The lookup of each byte lane in a table of 16 bytes.  Lane i of the result
// is the entry of table at the low four bits of lane i of indices.  The high
// four bits play no part, so each of the 256 values of a lane has a result.
// A multiply in GF(2^8) looks up the low and the high nibble of each byte in
// this way.
//
// The table repeats in each run of 16 lanes, and the lookup stays inside the
// run of its lane.  So 32 lanes take one vpshufb with AVX2, and 64 lanes take
// one vpshufb with AVX-512BW.  Each other width and target takes the generic
// shuffle of GCC: pshufb with SSSE3, tbl with NEON, and a split by the rules
// of GCC elsewhere.  The and with 15 merges with an and of the caller.  A loop
// that keeps one table repeats it one time, outside the loop.
template <int Lanes>
    requires NibbleLookupLanes<Lanes>
[[nodiscard]] CRUCIBLE_INLINE vec<std::uint8_t, Lanes> lookup_nibbles(vec<std::uint8_t, 16> table,
                                                                      vec<std::uint8_t, Lanes> indices) noexcept {
    using Bytes = vec<std::uint8_t, Lanes>;
    using Chars [[maybe_unused]] = detail::raw_vector_t<char, Lanes>;
    typename Bytes::raw_type const nibbles = (indices & Bytes{std::uint8_t{0x0f}}).raw();
    typename Bytes::raw_type const repeated =
        detail::repeat_nibble_table<Lanes>(table.raw(), detail::lane_list_t<Lanes>{});
#if defined(__AVX512BW__)
    // The masked form is the one builtin of the 512-bit vpshufb.  Each bit
    // of the mask is set, so each lane comes from the shuffle and none from
    // the merge source.
    if constexpr (Lanes == 64) {
        Chars const table_chars = __builtin_bit_cast(Chars, repeated);
        return Bytes{__builtin_bit_cast(
            typename Bytes::raw_type,
            __builtin_ia32_pshufb512_mask(table_chars, __builtin_bit_cast(Chars, nibbles), table_chars, ~0ULL))};
    }
#endif
#if defined(__AVX2__)
    if constexpr (Lanes == 32) {
        return Bytes{
            __builtin_bit_cast(typename Bytes::raw_type, __builtin_ia32_pshufb256(__builtin_bit_cast(Chars, repeated),
                                                                                  __builtin_bit_cast(Chars, nibbles)))};
    }
#endif
    return Bytes{__builtin_shuffle(repeated, nibbles)};
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
// does not throw.  It holds no linear value by value (HoldsNoLinearValue of
// foundation/Linear.h): the walk calls it for each run and ends it, and a
// call is no consume.  It stores no borrow outside the walk
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).
template <typename Body, typename V, typename First, typename Second>
concept RegisterRunBody = std::is_nothrow_invocable_v<Body&, core::View<First, sizeof(typename V::raw_type)>,
                                                      core::View<Second, sizeof(typename V::raw_type)>>
                       && ::foundation::linear::HoldsNoLinearValue<Body> && core::StoresNoBorrowOutside<Body>;

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
    return core::detail::for_each_chunk_pair_<sizeof(typename V::raw_type)>(first, second, body);
}

// A body that holds a linear value by value binds to this overload, so the
// refusal gives its reason (RegisterRunBody).
template <typename V, typename First, typename Second, typename Body>
    requires(!::foundation::linear::HoldsNoLinearValue<Body>)
void for_each_register_run(core::View<First> first, core::View<Second> second,
                           Body body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A body that can store a borrow outside the walk binds to this overload, so
// the refusal gives its reason (RegisterRunBody).
template <typename V, typename First, typename Second, typename Body>
    requires ::foundation::linear::HoldsNoLinearValue<Body> && (!core::StoresNoBorrowOutside<Body>)
void for_each_register_run(core::View<First> first, core::View<Second> second,
                           Body body) = delete(CRUCIBLE_STORING_BODY_REFUSAL);

// A body that takes the ViewMut of one register run of the run of a frame
// and the View of one register run of a second View, gives void, and does
// not throw.  It holds no linear value by value, and it stores no borrow
// outside the walk, as a RegisterRunBody does (ChunkPairMutBody of
// foundation/core/Region.h).
template <typename Body, typename V, typename First, typename Brand, typename Second>
concept RegisterRunMutBody = core::ChunkPairMutBody<Body, First, Brand, Second, sizeof(typename V::raw_type)>;

// The form of the run of a frame: body gets the ViewMut of each register run
// of first, with the brand of first, and the View of the run at the same
// position of second.  The door gives the ViewMut of the bytes of first after
// the last whole run, and the View of the same bytes of second.  The door
// takes first by move, so each run has one writer, and a store into a run
// takes the run by move.  The loop is the loop of the form above.
// Complexity: one call of body for each whole run.
template <typename V, typename First, ::foundation::brand::IsBrand Brand, std::size_t Extent, typename Second,
          typename Body>
    requires RegisterBytes<V, First, sizeof(typename V::raw_type)>
          && RegisterBytes<V, Second, sizeof(typename V::raw_type)> && RegisterRunMutBody<Body, V, First, Brand, Second>
CRUCIBLE_INLINE core::Record<core::ViewMut<First, Brand>, core::View<Second>>
for_each_register_run(core::ViewMut<First, Brand, Extent>&& first, core::View<Second> second, Body body) noexcept {
    return core::for_each_chunk_pair<sizeof(typename V::raw_type)>(std::move(first), second, std::move(body));
}

// A body that holds a linear value by value binds to this overload, so the
// refusal gives its reason (RegisterRunMutBody).
template <typename V, typename First, ::foundation::brand::IsBrand Brand, std::size_t Extent, typename Second,
          typename Body>
    requires(!::foundation::linear::HoldsNoLinearValue<Body>)
void for_each_register_run(core::ViewMut<First, Brand, Extent>&& first, core::View<Second> second,
                           Body body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A body that can store a borrow outside the walk binds to this overload, so
// the refusal gives its reason (RegisterRunMutBody).
template <typename V, typename First, ::foundation::brand::IsBrand Brand, std::size_t Extent, typename Second,
          typename Body>
    requires ::foundation::linear::HoldsNoLinearValue<Body> && (!core::StoresNoBorrowOutside<Body>)
void for_each_register_run(core::ViewMut<First, Brand, Extent>&& first, core::View<Second> second,
                           Body body) = delete(CRUCIBLE_STORING_BODY_REFUSAL);

// A named ViewMut stays with the caller, so the runs would be second writers
// of its run.  A const ViewMut writes nothing, so it gives no writer of a run.
template <typename V, typename First, ::foundation::brand::IsBrand Brand, std::size_t Extent, typename Second,
          typename Body>
void for_each_register_run(core::ViewMut<First, Brand, Extent>& first, core::View<Second> second,
                           Body body) = delete("the runs take the ViewMut by move: pass std::move(view)");
template <typename V, typename First, ::foundation::brand::IsBrand Brand, std::size_t Extent, typename Second,
          typename Body>
void for_each_register_run(core::ViewMut<First, Brand, Extent> const& first, core::View<Second> second,
                           Body body) = delete("a const ViewMut writes nothing, so it gives no writer of a run");

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

// The lanes of value, with the canonical NaN quiet_nan_of<ElementType> of
// foundation/core/Number.h in each lane that holds a NaN, as
// fp::canonicalize_nan of foundation/core/Float.h gives it for one value.
// Each other lane keeps its bits.  A lane holds a NaN when it is not equal to
// itself.  The compare is quiet, so it raises the invalid flag only for a
// signaling NaN, and no code of Crucible reads the flags.  One compare and
// one blend for each register: vcmpeqps and vpblendmd with AVX-512.  After a
// compare of the bits as integers, GCC 16 makes the blend into a second
// masked add of the op, so this form does not test the bits.
template <typename ElementType, int Lanes>
    requires core::Float<ElementType>
[[nodiscard]] CRUCIBLE_INLINE constexpr vec<ElementType, Lanes>
canonicalize_nans(vec<ElementType, Lanes> value) noexcept {
    using Bits = core::detail::FloatBits<ElementType>;
    using Words = vec<typename Bits::Word, Lanes>;
    typename vec<ElementType, Lanes>::raw_type const lanes = value.raw();
    typename Words::raw_type const bits = __builtin_bit_cast(typename Words::raw_type, lanes);
    typename Words::raw_type const canonical = lanes == lanes ? bits : Words{Bits::quiet_nan}.raw();
    return vec<ElementType, Lanes>{__builtin_bit_cast(typename vec<ElementType, Lanes>::raw_type, canonical)};
}

namespace detail {

// The lanes 2i + Offset of the concatenation of low and high, for each lane
// i of the result.  Offset is 0 for the even lanes and 1 for the odd lanes.
template <int Offset, typename Raw, int... Lane>
[[nodiscard]] CRUCIBLE_INLINE constexpr Raw pick_alternate_lanes(Raw low, Raw high,
                                                                 LaneList<Lane...> /*lanes*/) noexcept {
    return __builtin_shufflevector(low, high, (2 * Lane + Offset)...);
}

// The lanes 2i + Offset of value, for each lane i of a result of half the
// width.
template <int Offset, typename Half, typename Raw, int... Lane>
[[nodiscard]] CRUCIBLE_INLINE constexpr Half pick_alternate_half(Raw value, LaneList<Lane...> /*lanes*/) noexcept {
    return __builtin_shufflevector(value, value, (2 * Lane + Offset)...);
}

}  // namespace detail

// The even lanes and the odd lanes of the concatenation of low and high.
// Lane i of pick_even_lanes(low, high) is lane 2i of low when 2i is below Lanes,
// and lane 2i - Lanes of high otherwise.  pick_odd_lanes gives lane 2i + 1 in the
// same way.  An add of the two results gives the sums of the adjacent pairs
// of the 2 * Lanes elements, in order, with the lower element as the left
// operand.  A shuffle moves bits and rounds nothing.  With AVX-512 each
// shuffle is one vpermt2ps, and with NEON one uzp1 or uzp2.
template <typename ElementType, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE constexpr vec<ElementType, Lanes> pick_even_lanes(vec<ElementType, Lanes> low,
                                                                                vec<ElementType, Lanes> high) noexcept {
    return vec<ElementType, Lanes>{
        detail::pick_alternate_lanes<0>(low.raw(), high.raw(), detail::lane_list_t<Lanes>{})};
}

template <typename ElementType, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE constexpr vec<ElementType, Lanes> pick_odd_lanes(vec<ElementType, Lanes> low,
                                                                               vec<ElementType, Lanes> high) noexcept {
    return vec<ElementType, Lanes>{
        detail::pick_alternate_lanes<1>(low.raw(), high.raw(), detail::lane_list_t<Lanes>{})};
}

// The even lanes and the odd lanes of one vector, as a vector of half the
// lanes.  The half has at least two lanes, so value has at least four.
template <typename ElementType, int Lanes>
    requires(Lanes >= 4)
[[nodiscard]] CRUCIBLE_INLINE constexpr vec<ElementType, Lanes / 2>
pick_even_lanes(vec<ElementType, Lanes> value) noexcept {
    using Half = vec<ElementType, Lanes / 2>;
    return Half{detail::pick_alternate_half<0, typename Half::raw_type>(value.raw(), detail::lane_list_t<Lanes / 2>{})};
}

template <typename ElementType, int Lanes>
    requires(Lanes >= 4)
[[nodiscard]] CRUCIBLE_INLINE constexpr vec<ElementType, Lanes / 2>
pick_odd_lanes(vec<ElementType, Lanes> value) noexcept {
    using Half = vec<ElementType, Lanes / 2>;
    return Half{detail::pick_alternate_half<1, typename Half::raw_type>(value.raw(), detail::lane_list_t<Lanes / 2>{})};
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

// The sum wraps modulo two to the width of the lane.  The fold adds in the
// unsigned type of the lane, so a signed lane at its limit has no overflow,
// and the conversion back is modular.  A wrapping sum is associative and commutative, so the width
// that the build lowers the vector to does not change it.  A saturating sum
// is neither, so the sum does not saturate.
template <typename V>
    requires DetSafeSimd<V>
[[nodiscard]] CRUCIBLE_INLINE typename V::value_type reduce_add(V value) noexcept {
    using Unsigned = std::make_unsigned_t<typename V::value_type>;
    const typename V::raw_type lanes = value.raw();
    Unsigned acc = static_cast<Unsigned>(lanes[0]);
    for (int lane = 1; lane < static_cast<int>(V::size()); ++lane)
        acc = static_cast<Unsigned>(acc + static_cast<Unsigned>(lanes[lane]));
    return static_cast<typename V::value_type>(acc);
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

// The width in bytes of the widest vector register of the target: 64 with
// AVX-512F, 32 with AVX, and 16 on each other target, which covers SSE2 and
// NEON.  A kernel whose result does not depend on its count of lanes, such as
// the tree sums of foundation/core/Reduce.h, takes this width.  GCC splits a
// wider vector into registers of this width, and a 16-lane float vector on
// AVX2 then ran 16 times slower than an 8-lane one.
#if defined(__AVX512F__)
inline constexpr std::size_t native_vector_bytes = 64;
#elif defined(__AVX__)
inline constexpr std::size_t native_vector_bytes = 32;
#else
inline constexpr std::size_t native_vector_bytes = 16;
#endif

// The count of the lanes of ElementType in one register of
// native_vector_bytes.
template <typename ElementType>
    requires detail::VectorShape<ElementType, static_cast<int>(native_vector_bytes / sizeof(ElementType))>
inline constexpr int native_lanes = static_cast<int>(native_vector_bytes / sizeof(ElementType));

// The features of the host.  Each other target has none of them.  The level
// of the host is in foundation/os/X86Level.h, which also reads the register
// states that the OS saves.
#if defined(__x86_64__) || defined(__i386__)
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx512() noexcept { return __builtin_cpu_supports("avx512f"); }
[[nodiscard, gnu::pure]] inline bool runtime_supports_avx2() noexcept { return __builtin_cpu_supports("avx2"); }
[[nodiscard, gnu::pure]] inline bool runtime_supports_sse42() noexcept { return __builtin_cpu_supports("sse4.2"); }
#else
[[nodiscard]] constexpr bool runtime_supports_avx512() noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_avx2() noexcept { return false; }
[[nodiscard]] constexpr bool runtime_supports_sse42() noexcept { return false; }
#endif

}  // namespace CRUCIBLE_SIMD_TARGET
}  // namespace foundation::simd
