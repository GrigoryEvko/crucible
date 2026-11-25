// The compile-time checks of foundation/core/Scalar.h.

#include <foundation/core/Scalar.h>

#include <foundation/core/Arithmetic.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Convert.h>
#include <foundation/core/Id.h>
#include <foundation/core/Philox.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

// Four enumerators fill a field of two bits, in each order.
enum class Phase : std::uint8_t {
    forward = 0,
    backward = 1,
    optimizer = 2,
    other = 3
};
enum class Mixed : std::uint8_t {
    second = 2,
    zeroth = 0,
    third = 3,
    first = 1,
    beyond = 9
};
// A negative value and a field of one bit.
enum class Signed : std::int8_t {
    low = -1,
    zero = 0,
    one = 1
};
// Three enumerators fill no field of two bits.
enum class Three : std::uint8_t {
    zero = 0,
    one = 1,
    two = 2
};

static_assert(FullField<Phase, 1> && FullField<Phase, 2> && !FullField<Phase, 3>);
static_assert(FullField<Mixed, 2> && !FullField<Mixed, 3>);
static_assert(FullField<Signed, 1> && !FullField<Signed, 2>);
static_assert(FullField<Three, 1> && !FullField<Three, 2>);
static_assert(!FullField<Duplicated, 1>);
static_assert(!FullField<Phase, 0> && !FullField<Phase, 8> && !FullField<Phase, 64>);
static_assert(!FullField<Strong, 1> && !FullField<Switch, 1> && !FullField<int, 1>);
static_assert(enum_from_field<Phase, 2>(0u) == Phase::forward && enum_from_field<Phase, 2>(1u) == Phase::backward);
static_assert(enum_from_field<Phase, 2>(2u) == Phase::optimizer && enum_from_field<Phase, 2>(3u) == Phase::other);
static_assert(enum_from_field<Phase, 2>(std::uint8_t{0xF6}) == Phase::optimizer);
static_assert(enum_from_field<Phase, 2>(-1) == Phase::other);
static_assert(enum_from_field<Mixed, 2>(std::uint64_t{0x5}) == Mixed::first);
static_assert(enum_from_field<Signed, 1>(std::uint16_t{0xFFFE}) == Signed::zero);

// enum_at gives the values in ascending order, each one time, and no value
// past the last.
template <class E>
[[nodiscard]] consteval bool is_at(std::size_t index, E expected) {
    return enum_at<E>(index).value_or(expected) == expected && enum_at<E>(index).is_some();
}

static_assert(is_at(0, Mixed::zeroth) && is_at(1, Mixed::first) && is_at(3, Mixed::third));
static_assert(is_at(4, Mixed::beyond) && enum_at<Mixed>(5).is_none());
static_assert(is_at(0, Unsorted::first) && is_at(1, Unsorted::second) && is_at(2, Unsorted::third));
static_assert(is_at(0, Duplicated::first) && is_at(1, Duplicated::second) && enum_at<Duplicated>(2).is_none());
static_assert(is_at(0, Shifted::low) && is_at(2, Shifted::high) && enum_at<Shifted>(3).is_none());
static_assert(is_at(0, Extremes::lowest) && is_at(2, Extremes::highest));
static_assert(is_at(0, Switch::off) && is_at(1, Switch::on) && enum_at<Switch>(2).is_none());
static_assert(enum_at<Wide>(~std::size_t{0}).is_none());

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
struct HoldsOnlyAnUnnamedBitField {
    std::uint32_t : 32;
};

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
// the walk cannot read, are refused.  A closure is a class whose captures are
// its members, so a closure that holds one word by copy is plain.
static_assert(!DecodableFrom<Annotated, std::uint32_t> && !DecodableFrom<HoldsSeal, std::uint32_t>);
static_assert(!DecodableFrom<HoldsOnlyAnUnnamedBitField, std::uint32_t>);
static_assert(DecodableFrom<CapturesWord, std::uint32_t> && DecodableFrom<std::uint32_t, CapturesWord>);

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

// ── Open enumerations ─────────────────────────────────────────────────

// The two enumerators name two values, and each other value of the
// underlying type is a value too.
enum class[[= open_values{}]] Code : std::uint16_t {
    one = 1,
    two = 2
};
// An open enumeration beside a closed one, in four bytes with no padding.
struct CodeAndPhase {
    Code code = Code::one;
    Contiguous phase = Contiguous::first;
    std::uint8_t count = 0;
};
static_assert(sizeof(CodeAndPhase) == 4);

static_assert(OpenEnumeration<Code> && !ClosedEnumeration<Code> && Enumerated<Code>);
static_assert(ClosedEnumeration<Contiguous> && !OpenEnumeration<Contiguous> && ClosedEnumeration<Switch>);
static_assert(!OpenEnumeration<Strong> && !ClosedEnumeration<Strong> && !OpenEnumeration<int>);

// enum_from of an open enumeration is total: it gives the value, and no
// Option.  The closed form still gives no value for a number with no name.
static_assert(std::is_same_v<decltype(enum_from<Code>(std::uint16_t{1})), Code>);
static_assert(enum_from<Code>(1) == Code::one && std::to_underlying(enum_from<Code>(300)) == 300);
static_assert(std::to_underlying(enum_from<Code>(0)) == 0 && std::to_underlying(enum_from<Code>(65535)) == 65535);
static_assert(enum_from<Contiguous>(6).is_none() && enum_from<Contiguous>(5).is_some());

// decode of an open enumeration takes each value.  It still refuses a value
// of a closed part that names no enumerator.
static_assert(std::to_underlying(decode<Code>(std::uint16_t{300}).value_or(Code::one)) == 300);
static_assert(decode<Code>(std::uint16_t{0}).is_some() && decode<Code>(std::uint16_t{65535}).is_some());
static_assert(std::to_underlying(decode<CodeAndPhase>(std::uint32_t{0x0704012C}).value_or(CodeAndPhase{}).code) == 300);
static_assert(decode<CodeAndPhase>(std::uint32_t{0x0706012C}).is_none());

// An open enumeration with no niche is bit valid, so bit_cast makes it with
// no check.  A class with a closed part is not.
static_assert(BitValid<Code> && !BitValid<CodeAndPhase>);
static_assert(std::to_underlying(bit_cast<Code>(std::uint16_t{300})) == 300);

// An open enumeration fills each field that its underlying type holds.
static_assert(FullField<Code, 1> && FullField<Code, 15> && FullField<Code, 16>);
static_assert(!FullField<Code, 0> && !FullField<Code, 17>);
static_assert(std::to_underlying(enum_from_field<Code, 16>(std::uint32_t{0x1234ABCD})) == 0xABCD);
static_assert(std::to_underlying(enum_from_field<Code, 4>(std::uint8_t{0xF7})) == 7);

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

// Two bases, and the first one is private.
struct PairThenWords : private PrivatePair, Words {
    constexpr PairThenWords(PrivatePair pair, Words words) noexcept : PrivatePair{pair}, Words{words} {}

    std::uint8_t tail = 0;
};

// The fields of each base in the order of the walk, as decimal digit groups
// of six for each base.
template <class T>
[[nodiscard]] consteval std::uint64_t base_fold(T const& object) {
    std::uint64_t fold = 0;
    for_each_base(object, [&](auto const& base) noexcept { fold = fold * 1000000 + field_fold(base); });
    return fold;
}

template <class T>
[[nodiscard]] consteval std::size_t base_calls(T const& object) {
    std::size_t count = 0;
    for_each_base(object, [&](auto const&) noexcept { ++count; });
    return count;
}

// The walk of the bases reads each direct base one time, a private base
// too, in the order of the declarations.  A class with no base gives no
// call.
static_assert(base_fold(WordsAndTail{{1, 2}, 3}) == 1002 && base_calls(WordsAndTail{{1, 2}, 3}) == 1);
static_assert(base_fold(PairThenWords{PrivatePair{7, 9}, Words{1, 2}}) == 7009001002);
static_assert(base_calls(PairThenWords{PrivatePair{7, 9}, Words{1, 2}}) == 2);
static_assert(base_calls(Triple{1, 2, 3}) == 0 && base_calls(Token{}) == 0);

// A private View, the private state of a class that keeps an invariant in
// its copy, and a private sealed member stay private.
class PrivateAddress {
    View<int const> elements_{};
};
class PrivateCountedWord {
public:
    PrivateCountedWord(PrivateCountedWord const& other) noexcept : word_{other.word_ + 1} {}

private:
    std::uint32_t word_ = 0;
};
class PrivateSealedWord {
    std::uint64_t word_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct PublicAddress {
    View<int const> elements{};
};

// The walk takes a class that hides no state, and a plain class that is
// trivially copyable.  It refuses each other class.
static_assert(FieldWalkable<PrivatePair> && FieldWalkable<Triple> && FieldWalkable<Token>);
static_assert(FieldWalkable<WordsAndTail> && FieldWalkable<PublicAddress>);
static_assert(!FieldWalkable<PrivateAddress> && !FieldWalkable<PrivateCountedWord>);
static_assert(!FieldWalkable<PrivateSealedWord> && !FieldWalkable<int>);

// The walk of the bases takes the FieldWalkable classes, as the walk of the
// fields does.  A class whose base hides an address is refused.
struct HidesAddressInBase : PrivateAddress {};
static_assert(FieldWalkable<PairThenWords> && !FieldWalkable<HidesAddressInBase>);

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.7: each name of the API table and of
// the gates, and the accessors lo and hi of Bounds and the halves low and
// high of WideProduct, which the design of the unit gives.
inline constexpr TextView scalar_names{
    "Integer SignedInteger UnsignedInteger Float Arithmetic Enumerated OpenEnumeration ClosedEnumeration "
    "DecodableFrom BitValid FieldWalkable "
    "Exchangeable "
    "fits_each_value max_of min_of bits_of value_bits_of min_normal_of min_subnormal_of epsilon_of infinity_of "
    "quiet_nan_of "
    "signaling_nan_of min max Bounds of lo hi bounds clamp compare fits unsigned_abs abs_diff narrow "
    "wrapping_narrow saturating_narrow widen enum_from enum_at FullField enum_from_field checked_add checked_sub "
    "checked_mul "
    "checked_div checked_rem "
    "checked_neg checked_shl checked_shr wrapping_add wrapping_sub wrapping_mul wrapping_neg wrapping_shl "
    "wrapping_shr saturating_add saturating_sub saturating_mul gcd widening_mul WideProduct low high popcount "
    "countl_zero countr_zero countl_one countr_one bit_width has_single_bit bit_floor bit_ceil rotl rotr low_mask "
    "byteswap bit_cast decode for_each_field for_each_base is_nan is_inf is_finite is_normal sign_bit abs copy_sign sqrt fma total_key exchange "
    "swap Philox4x32 generate Counter Key multiplier0 multiplier1 weyl_step0 weyl_step1 Id raw Digest Quantity "
    "QuantityUnit convert round_to_float add subtract multiply divide dimension scale Dimension memory count time "
    "work clock Scale power Derived Product Quotient Byte Element Nanosecond Microsecond Millisecond Second Cycle "
    "Flop BytePerSecond FlopPerSecond FlopPerByte"};

CRUCIBLE_CLOSED_SURFACE(scalar_names, WideProduct<std::uint8_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, WideProduct<std::uint64_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Bounds<int>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Bounds<double>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Quantity<unit::Byte, std::uint64_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Quantity<unit::Second, std::int8_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Quantity<unit::BytePerSecond, double>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, unit::Second);
CRUCIBLE_CLOSED_SURFACE(scalar_names, unit::Dimension);
CRUCIBLE_CLOSED_SURFACE(scalar_names, unit::Scale);
CRUCIBLE_CLOSED_SURFACE(scalar_names, unit::Product<unit::Byte, unit::Byte>);
struct SurfaceTag;
CRUCIBLE_CLOSED_SURFACE(scalar_names, Id<SurfaceTag, std::uint32_t>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Digest<SurfaceTag>);
CRUCIBLE_CLOSED_SURFACE(scalar_names, Philox4x32);

// The walks of the fields and of the bases refuse a visitor that can store a
// borrow outside the walk, and admit the same visitor with a count.
template <class Kept>
struct VisitThatKeeps final {
    Kept& kept;
    template <class Part>
    constexpr void operator()(Part const& /*part*/) const noexcept {}
};
template <class Visit>
constexpr bool walks_fields_with = requires(WordsAndTail const& object, Visit visit) { for_each_field(object, visit); };
template <class Visit>
constexpr bool walks_bases_with = requires(WordsAndTail const& object, Visit visit) { for_each_base(object, visit); };
static_assert(!walks_fields_with<VisitThatKeeps<Option<View<int const>>>> && walks_fields_with<VisitThatKeeps<int>>);
static_assert(!walks_bases_with<VisitThatKeeps<Option<View<int const>>>> && walks_bases_with<VisitThatKeeps<int>>);

}  // namespace detail::scalar_checks

}  // namespace foundation::core
