// The compile-time checks of foundation/core/Scalar.h.

#include <foundation/core/Scalar.h>

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Convert.h>
#include <foundation/core/Id.h>
#include <foundation/core/Philox.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
struct HalfWords {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
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
// Each bit of the source is a value bit.  A float and a double have no
// padding bit, and long double has 80 value bits in 16 bytes on x86_64.
static_assert(!DecodableFrom<std::uint64_t, Padded> && DecodableFrom<std::uint32_t, float>);
static_assert(DecodableFrom<std::uint64_t, double> && !DecodableFrom<Words, long double>);
static_assert(DecodableFrom<std::uint64_t, HalfWords> && DecodableFrom<HalfWords, double>);
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

// A word whose highest value is the empty value of its niche, through the
// members of the niche protocol.
struct NicheWord {
    std::uint32_t value = 0;

    [[nodiscard]] static constexpr NicheWord niche_empty(niche_key<NicheWord> const&) noexcept {
        return NicheWord{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<NicheWord> const&) const noexcept {
        return value == 0xFFFFFFFFu;
    }
};
// A niche part beside a bool, and three bytes of padding.
struct FlaggedWord {
    NicheWord word{};
    bool is_ready = false;
};
static_assert(sizeof(Option<NicheWord>) == sizeof(NicheWord) && sizeof(FlaggedWord) == 8);

// decode gives no value for the empty value of a niche, of the whole To and
// of a part.  The bool of a part is checked before the niche.
static_assert(decode<NicheWord>(std::uint32_t{7}).is_some() && decode<NicheWord>(std::uint32_t{0xFFFFFFFEu}).is_some());
static_assert(decode<NicheWord>(std::uint32_t{0xFFFFFFFFu}).is_none());
static_assert(decode<FlaggedWord>(std::uint64_t{0x0000000100000007}).is_some());
static_assert(decode<FlaggedWord>(std::uint64_t{0x00000001FFFFFFFF}).is_none());
static_assert(decode<FlaggedWord>(std::uint64_t{0x0000000200000007}).is_none());

// ── The bit copy ──────────────────────────────────────────────────────

// Each bit pattern of a bit valid type is a value: no bool, no enumeration
// with an enumerator and no niche.
static_assert(BitValid<std::uint32_t> && BitValid<float> && BitValid<double> && BitValid<Strong>);
static_assert(BitValid<std::byte> && BitValid<Words> && BitValid<Triple> && BitValid<HalfWords>);
static_assert(!BitValid<bool> && !BitValid<Contiguous> && !BitValid<Switch> && !BitValid<Flagged>);
static_assert(!BitValid<NicheWord> && !BitValid<FlaggedWord> && !BitValid<int*> && !BitValid<Annotated>);
static_assert(!BitValid<HoldsSeal> && !BitValid<Token> && !BitValid<void> && !BitValid<int&>);

// The IEEE 754 bits of 1.0 and of -0.0f, and back.
static_assert(bit_cast<std::uint64_t>(1.0) == 0x3FF0000000000000u && bit_cast<std::uint32_t>(-0.0f) == 0x80000000u);
static_assert(bit_cast<std::uint32_t>(bit_cast<float>(std::uint32_t{0x7FC00001u})) == 0x7FC00001u);
static_assert(bit_cast<Words>(Words{1, 2}).high == 2 && bit_cast<Strong>(std::uint8_t{200}) == Strong{200});
static_assert(std::is_same_v<decltype(bit_cast<double>(std::uint64_t{0})), double>);

// ── The field walk ────────────────────────────────────────────────────

// Two private members, as the value of a strong id is private.
class PrivatePair {
public:
    constexpr PrivatePair(std::uint32_t first, std::uint16_t second) noexcept : first_{first}, second_{second} {}

private:
    std::uint32_t first_ = 0;
    std::uint16_t second_ = 0;
};
// One member of its own beside the two members of a base.
struct WordsAndTail : Words {
    std::uint8_t tail = 0;
};

// The fields in the order of the walk, as decimal digit groups of three.
template <class T>
[[nodiscard]] consteval std::uint64_t field_fold(T const& object) {
    std::uint64_t fold = 0;
    for_each_field(object, [&](auto const& field) noexcept { fold = fold * 1000 + widen<std::uint64_t>(field); });
    return fold;
}

template <class T>
[[nodiscard]] consteval std::size_t field_count(T const& object) {
    std::size_t count = 0;
    for_each_field(object, [&](auto const&) noexcept { ++count; });
    return count;
}

// The walk reads the private members in the order of the declarations, each
// member one time.  An empty class gives no call, and a member of a base is
// no member of the class.
static_assert(field_fold(PrivatePair{7, 9}) == 7009 && field_count(PrivatePair{7, 9}) == 2);
static_assert(field_fold(Triple{1, 2, 3}) == 1002003 && field_count(Token{}) == 0);
static_assert(field_fold(WordsAndTail{{1, 2}, 3}) == 3 && field_count(WordsAndTail{{1, 2}, 3}) == 1);

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.7: each name of the API table and of
// the gates, and the accessors lo and hi of Bounds and the halves low and
// high of WideProduct, which the design of the unit gives.
inline constexpr TextView scalar_names{
    "Integer SignedInteger UnsignedInteger Float Arithmetic Enumerated DecodableFrom BitValid Exchangeable "
    "fits_each_value max_of min_of bits_of value_bits_of min_normal_of min_subnormal_of epsilon_of infinity_of "
    "quiet_nan_of "
    "signaling_nan_of min max Bounds of lo hi bounds clamp compare fits unsigned_abs abs_diff narrow "
    "wrapping_narrow saturating_narrow widen enum_from checked_add checked_sub checked_mul checked_div checked_rem "
    "checked_neg checked_shl checked_shr wrapping_add wrapping_sub wrapping_mul wrapping_neg wrapping_shl "
    "wrapping_shr saturating_add saturating_sub saturating_mul gcd widening_mul WideProduct low high popcount "
    "countl_zero countr_zero countl_one countr_one bit_width has_single_bit bit_floor bit_ceil rotl rotr low_mask "
    "byteswap bit_cast decode for_each_field is_nan is_inf is_finite is_normal sign_bit abs copy_sign sqrt fma total_key exchange "
    "swap Philox4x32 generate Counter Key multiplier0 multiplier1 weyl_step0 weyl_step1 Id raw Digest Quantity "
    "QuantityUnit convert dimension ratio Byte Element Nanosecond Microsecond Millisecond Second Cycle Memory Count "
    "Time Clock"};

CRUCIBLE_CLOSED_SURFACE(scalar_names, WideProduct<std::uint8_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, WideProduct<std::uint64_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Bounds<int>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Bounds<double>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Quantity<unit::Byte, std::uint64_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Quantity<unit::Second, std::int8_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, unit::Second);
CRUCIBLE_CLOSED_SURFACE(scalar_names, unit::Memory);
struct SurfaceTag;
CRUCIBLE_CLOSED_SURFACE(scalar_names, Id<SurfaceTag, std::uint32_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Digest<SurfaceTag>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Philox4x32);

}  // namespace detail::scalar_checks

}  // namespace foundation::core
