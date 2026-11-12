// The compile-time checks of foundation/core/Scalar.h.

#include <foundation/core/Scalar.h>

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Verbs.h>

#include <cstdint>

namespace foundation::core {

namespace detail::scalar_checks {

// One range with no gap: the check is one compare.
enum class Contiguous : std::uint8_t {
    first = 3,
    second,
    third
};
// One range of negative values.
enum class Shifted : std::int8_t {
    low = -3,
    middle = -2,
    high = -1
};
// Gaps inside 64 consecutive values: one compare and one bit test.
enum class Gapped : std::uint8_t {
    first = 1,
    second = 5,
    third = 64
};
// Enumerators that the declaration does not sort.
enum class Unsorted : std::uint8_t {
    third = 9,
    first = 1,
    second = 4
};
// Gaps over more than 64 values: a binary search.
enum class Wide : std::uint16_t {
    first = 1,
    second = 300,
    third = 60000
};
// The two extremes of a signed type of 64 bits.
enum class Extremes : std::int64_t {
    lowest = INT64_MIN,
    zero = 0,
    highest = INT64_MAX
};
// Two enumerators with one value.
enum class Duplicated : std::uint8_t {
    first = 2,
    alias = 2,
    second = 3
};
// A bool as the underlying type.
enum class Switch : bool {
    off = false,
    on = true
};
enum class OnlyOn : bool {
    on = true
};
// No enumerator: each value of the underlying type is a value.
enum class Strong : std::uint8_t {
};
// An unscoped enumeration with a fixed underlying type.
enum Unscoped : std::uint8_t {
    unscoped_first = 7,
    unscoped_second = 9
};

template <class E>
[[nodiscard]] consteval bool names(std::underlying_type_t<E> raw) {
    return enum_from<E>(raw).is_some();
}

static_assert(Enumerated<Contiguous> && Enumerated<Switch> && Enumerated<Unscoped>);
static_assert(!Enumerated<Strong> && !Enumerated<int> && !Enumerated<bool>);

static_assert(!names<Contiguous>(2) && names<Contiguous>(3) && names<Contiguous>(5) && !names<Contiguous>(6));
static_assert(!names<Contiguous>(255) && !names<Contiguous>(0));
static_assert(enum_from<Contiguous>(4).value_or(Contiguous::first) == Contiguous::second);
static_assert(!names<Shifted>(-4) && names<Shifted>(-3) && names<Shifted>(-1) && !names<Shifted>(0));
static_assert(!names<Shifted>(127) && !names<Shifted>(-128));
static_assert(!names<Gapped>(0) && names<Gapped>(1) && !names<Gapped>(2) && names<Gapped>(5));
static_assert(!names<Gapped>(63) && names<Gapped>(64) && !names<Gapped>(65) && !names<Gapped>(255));
static_assert(names<Unsorted>(1) && names<Unsorted>(4) && names<Unsorted>(9) && !names<Unsorted>(5));
static_assert(!names<Wide>(0) && names<Wide>(1) && !names<Wide>(299) && names<Wide>(300));
static_assert(names<Wide>(60000) && !names<Wide>(60001) && !names<Wide>(65535));
static_assert(names<Extremes>(INT64_MIN) && !names<Extremes>(INT64_MIN + 1) && names<Extremes>(0));
static_assert(!names<Extremes>(-1) && !names<Extremes>(1) && names<Extremes>(INT64_MAX));
static_assert(!names<Duplicated>(1) && names<Duplicated>(2) && names<Duplicated>(3) && !names<Duplicated>(4));
static_assert(names<Switch>(false) && names<Switch>(true));
static_assert(names<OnlyOn>(true) && !names<OnlyOn>(false));
static_assert(names<Unscoped>(7) && !names<Unscoped>(8) && names<Unscoped>(9));

// A bool and an enumeration in two bytes.
struct Flagged {
    bool is_ready = false;
    Contiguous phase = Contiguous::first;
};
// A base and a nested member: six bytes with no padding.
struct Header {
    Shifted level = Shifted::low;
    std::uint8_t count = 0;
};
struct Nested : Header {
    Flagged flags{};
    std::uint16_t word = 0;
};
static_assert(sizeof(Nested) == 6);
// The sources: words with one bit pattern for each value.
struct Triple {
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    std::uint16_t third = 0;
};
struct Words {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};
// Six bytes of members and two bytes of padding.
struct Padded {
    std::uint32_t key = 0;
    std::uint16_t tag = 0;
};
// Each other shape that decode refuses.
struct Token {};
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] Annotated {
    std::uint32_t value = 0;
};
struct HoldsSeal {
    std::uint32_t value = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal{};
};
struct HoldsToken {
    std::uint32_t value = 0;
    [[no_unique_address]] Token token{};
};
struct WithBitField {
    std::uint32_t low : 16;
    std::uint32_t high : 16;
};
using CapturesWord = decltype([word = std::uint32_t{0}] { return word; });

static_assert(DecodableFrom<bool, std::uint8_t> && DecodableFrom<Contiguous, std::uint8_t>);
static_assert(DecodableFrom<Strong, std::uint8_t> && DecodableFrom<Switch, std::uint8_t>);
static_assert(DecodableFrom<float, std::uint32_t> && DecodableFrom<double, std::uint64_t>);
static_assert(DecodableFrom<Flagged, std::uint16_t> && DecodableFrom<Nested, Triple>);
static_assert(DecodableFrom<Words, Words> && DecodableFrom<std::uint32_t, std::uint8_t const[4]>);
static_assert(DecodableFrom<Extremes, std::int64_t>);
// The two types must have one size.
static_assert(!DecodableFrom<std::uint32_t, std::uint16_t> && !DecodableFrom<std::uint16_t, std::uint32_t>);
// Each bit of the source is a value bit.
static_assert(!DecodableFrom<std::uint64_t, Padded> && !DecodableFrom<std::uint32_t, float>);
static_assert(!DecodableFrom<std::uint64_t, double>);
// A function cannot return an array or a cv-qualified value.
static_assert(!DecodableFrom<std::uint8_t[4], std::uint32_t> && !DecodableFrom<std::uint32_t const, std::uint32_t>);
// An address, an empty class and a bit-field are refused.  The negative
// fixture neg_core_decode_union holds the union.
static_assert(!DecodableFrom<int*, std::uint64_t> && !DecodableFrom<std::uint64_t, int*>);
static_assert(!DecodableFrom<int Flagged::*, std::uint64_t> && !DecodableFrom<decltype(nullptr), std::uint64_t>);
static_assert(!DecodableFrom<Token, std::uint8_t> && !DecodableFrom<HoldsToken, std::uint32_t>);
static_assert(!DecodableFrom<WithBitField, std::uint32_t>);
// A class whose values only its constructors make, and a class whose state
// the walk cannot read, are refused.
static_assert(!DecodableFrom<Annotated, std::uint32_t> && !DecodableFrom<HoldsSeal, std::uint32_t>);
static_assert(!DecodableFrom<CapturesWord, std::uint32_t> && !DecodableFrom<std::uint32_t, CapturesWord>);

// The byte order of the two hosts is little-endian, so the first byte of a
// word is its low byte.
static_assert(decode<bool>(std::uint8_t{1}).value_or(false) && !decode<bool>(std::uint8_t{0}).value_or(true));
static_assert(decode<bool>(std::uint8_t{2}).is_none() && decode<bool>(std::uint8_t{255}).is_none());
static_assert(decode<Contiguous>(std::uint8_t{4}).value_or(Contiguous::first) == Contiguous::second);
static_assert(decode<Contiguous>(std::uint8_t{6}).is_none() && decode<Strong>(std::uint8_t{200}).is_some());
static_assert(decode<Switch>(std::uint8_t{1}).is_some() && decode<Switch>(std::uint8_t{2}).is_none());
static_assert(decode<OnlyOn>(std::uint8_t{0}).is_none() && decode<OnlyOn>(std::uint8_t{1}).is_some());
static_assert(decode<Flagged>(std::uint16_t{0x0401}).is_some());
static_assert(decode<Flagged>(std::uint16_t{0x0402}).is_none() && decode<Flagged>(std::uint16_t{0x0701}).is_none());
static_assert(decode<float>(std::uint32_t{0x7fc00001}).is_some() && decode<Words>(Words{3, 4}).is_some());

[[nodiscard]] consteval bool decodes_nested_parts() {
    Nested const nested = decode<Nested>(Triple{0x09FE, 0x0501, 0x1234}).value_or(Nested{});
    return nested.level == Shifted::middle && nested.count == 9 && nested.flags.is_ready
        && nested.flags.phase == Contiguous::third && nested.word == 0x1234;
}
static_assert(decodes_nested_parts());
// A level that names no enumerator, and a bool byte of 3.
static_assert(decode<Nested>(Triple{0x0905, 0x0501, 0}).is_none());
static_assert(decode<Nested>(Triple{0x09FE, 0x0503, 0}).is_none());

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.7: each name of the API table and of
// the gates, and the accessors lo and hi of Bounds and the halves low and
// high of WideProduct, which the design of the unit gives.
inline constexpr TextView scalar_names{
    "Integer SignedInteger UnsignedInteger Float Arithmetic Enumerated DecodableFrom BitValid fits_each_value "
    "max_of min_of bits_of value_bits_of min_normal_of min_subnormal_of epsilon_of infinity_of quiet_nan_of "
    "signaling_nan_of min max Bounds of lo hi bounds clamp compare fits unsigned_abs abs_diff narrow "
    "wrapping_narrow saturating_narrow widen enum_from checked_add checked_sub checked_mul checked_div checked_rem "
    "checked_neg checked_shl checked_shr wrapping_add wrapping_sub wrapping_mul wrapping_neg wrapping_shl "
    "wrapping_shr saturating_add saturating_sub saturating_mul gcd widening_mul WideProduct low high popcount "
    "countl_zero countr_zero countl_one countr_one bit_width has_single_bit bit_floor bit_ceil rotl rotr low_mask "
    "byteswap bit_cast decode is_nan is_inf is_finite is_normal sign_bit abs copy_sign sqrt fma total_key exchange "
    "swap Philox4x32 generate Id raw Digest Quantity convert niche_empty is_niche_empty"};

CRUCIBLE_CLOSED_SURFACE(scalar_names, WideProduct<std::uint8_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, WideProduct<std::uint64_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Bounds<int>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Bounds<double>);

}  // namespace detail::scalar_checks

}  // namespace foundation::core
