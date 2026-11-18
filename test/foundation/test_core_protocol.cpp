// Tests of foundation/core/Equatable.h and foundation/core/Protocol.h: equal,
// order, hash, encode, decode and format of a class that derives them from
// its parts.  The oracles are hand forms of each operation over the same
// fields, written here and not in the headers.

#include <foundation/core/Protocol.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>

#include "philox_stream.h"

#include <compare>
#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::bit_cast;
using ::foundation::core::decode;
using ::foundation::core::DecodeFault;
using ::foundation::core::DecodeRefusal;
using ::foundation::core::derive_protocol;
using ::foundation::core::Digest;
using ::foundation::core::encode;
using ::foundation::core::encoded_size_v;
using ::foundation::core::equal;
using ::foundation::core::FixedArray;
using ::foundation::core::FixedText;
using ::foundation::core::format;
using ::foundation::core::hash;
using ::foundation::core::Index;
using ::foundation::core::index;
using ::foundation::core::local_part;
using ::foundation::core::order;
using ::foundation::core::pad_part;
using ::foundation::core::TextView;

enum class Phase : std::uint8_t {
    forward = 0,
    backward = 1,
    // A gap: the values 2 to 6 name no enumerator.
    other = 7,
};

// A strong id with a private word, as the strong ids of the product are.
class[[= derive_protocol{}]] Ticket {
public:
    constexpr Ticket() noexcept = default;
    constexpr explicit Ticket(std::uint32_t word) noexcept : word_{word} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return word_; }

private:
    std::uint32_t word_ = 0;
};

struct[[= derive_protocol{}]] Header {
    std::int16_t tag = 0;
};

// Each kind of part: a base, the integers of each width and sign, a bool,
// an enumeration with a gap, a float and a double, two FixedArrays, a nested
// class, a pad part and a local part.  The probe region at the end holds a C
// array part and a pointer part.
struct[[= derive_protocol{}]] Sample : Header {
    std::uint8_t small = 0;
    std::int8_t signed_small = 0;
    bool flag = false;
    Phase phase = Phase::forward;
    [[= pad_part{}]] FixedArray<std::uint8_t, 2> pad{};
    std::uint32_t word = 0;
    std::int64_t wide = 0;
    float ratio = 0.0f;
    double scale = 0.0;
    FixedArray<std::uint16_t, 3> lanes{};
    FixedArray<std::int32_t, 2> pairs{};
    Ticket ticket{};
    [[= local_part{}]] std::uint64_t address = 0;
};

// The image of a Sample: 2 + 1 + 1 + 1 + 1 + 2 + 4 + 8 + 4 + 8 + 6 + 8 + 4
// bytes, and no byte of the local part.
static_assert(encoded_size_v<Sample> == 50);

// The offsets of the image of a Sample, in declaration order, the base
// first.
constexpr std::size_t kTagAt = 0;
constexpr std::size_t kSmallAt = 2;
constexpr std::size_t kSignedSmallAt = 3;
constexpr std::size_t kFlagAt = 4;
constexpr std::size_t kPhaseAt = 5;
constexpr std::size_t kPadAt = 6;
constexpr std::size_t kWordAt = 8;
constexpr std::size_t kWideAt = 12;
constexpr std::size_t kRatioAt = 20;
constexpr std::size_t kScaleAt = 24;
constexpr std::size_t kLanesAt = 32;
constexpr std::size_t kPairsAt = 38;
constexpr std::size_t kTicketAt = 46;

using Image = FixedArray<std::byte, encoded_size_v<Sample>>;

// A random Sample, with each leaf from the stream.  The float and the
// double take random bits, so the stream reaches -0, a NaN and a subnormal.
[[nodiscard]] Sample random_sample(::crucible::test::PhiloxStream& stream) noexcept {
    constexpr FixedArray<Phase, 3> phases{Phase::forward, Phase::backward, Phase::other};
    Sample sample{};
    sample.tag = static_cast<std::int16_t>(stream.next_word());
    sample.small = static_cast<std::uint8_t>(stream.next_word());
    sample.signed_small = static_cast<std::int8_t>(stream.next_word());
    sample.flag = (stream.next_word() & 1U) != 0;
    sample.phase = phases.at(Index<3>::check(stream.below(3)).expect("below 3 is an index of the phases"));
    sample.word = stream.next_word();
    sample.wide = static_cast<std::int64_t>(stream.next_wide());
    sample.ratio = bit_cast<float>(stream.next_word());
    sample.scale = bit_cast<double>(stream.next_wide());
    for (auto const index : decltype(sample.lanes)::indices()) {
        sample.lanes.at(index) = static_cast<std::uint16_t>(stream.next_word());
    }
    for (auto const index : decltype(sample.pairs)::indices()) {
        sample.pairs.at(index) = static_cast<std::int32_t>(stream.next_word());
    }
    sample.ticket = Ticket{stream.next_word()};
    sample.address = stream.next_wide();
    return sample;
}

// The hand compare of two Samples: each field with ==, a float by its bits.
[[nodiscard]] bool hand_equal(Sample const& left, Sample const& right) noexcept {
    bool is_same = left.tag == right.tag && left.small == right.small && left.signed_small == right.signed_small
                && left.flag == right.flag && left.phase == right.phase && left.pad == right.pad
                && left.word == right.word && left.wide == right.wide
                && bit_cast<std::uint32_t>(left.ratio) == bit_cast<std::uint32_t>(right.ratio)
                && bit_cast<std::uint64_t>(left.scale) == bit_cast<std::uint64_t>(right.scale)
                && left.ticket.raw() == right.ticket.raw() && left.address == right.address;
    for (auto const index : decltype(left.lanes)::indices()) {
        is_same = is_same && left.lanes.at(index) == right.lanes.at(index);
    }
    for (auto const index : decltype(left.pairs)::indices()) {
        is_same = is_same && left.pairs.at(index) == right.pairs.at(index);
    }
    return is_same;
}

// The key of the totalOrder of IEEE 754 for a double, by hand.
[[nodiscard]] std::int64_t total_key(double value) noexcept {
    std::int64_t const bits = bit_cast<std::int64_t>(value);
    return bits < 0 ? static_cast<std::int64_t>(bits ^ 0x7FFFFFFFFFFFFFFFLL) : bits;
}

[[nodiscard]] std::int32_t total_key(float value) noexcept {
    std::int32_t const bits = bit_cast<std::int32_t>(value);
    return bits < 0 ? bits ^ 0x7FFFFFFF : bits;
}

// The hand order of two Samples: lexicographic over the fields in
// declaration order.
[[nodiscard]] std::strong_ordering hand_order(Sample const& left, Sample const& right) noexcept {
    if (auto const step = left.tag <=> right.tag; std::is_neq(step)) return step;
    if (auto const step = left.small <=> right.small; std::is_neq(step)) return step;
    if (auto const step = left.signed_small <=> right.signed_small; std::is_neq(step)) return step;
    if (auto const step = left.flag <=> right.flag; std::is_neq(step)) return step;
    if (auto const step = left.phase <=> right.phase; std::is_neq(step)) return step;
    for (auto const index : decltype(left.pad)::indices()) {
        if (auto const step = left.pad.at(index) <=> right.pad.at(index); std::is_neq(step)) return step;
    }
    if (auto const step = left.word <=> right.word; std::is_neq(step)) return step;
    if (auto const step = left.wide <=> right.wide; std::is_neq(step)) return step;
    if (auto const step = total_key(left.ratio) <=> total_key(right.ratio); std::is_neq(step)) return step;
    if (auto const step = total_key(left.scale) <=> total_key(right.scale); std::is_neq(step)) return step;
    for (auto const index : decltype(left.lanes)::indices()) {
        if (auto const step = left.lanes.at(index) <=> right.lanes.at(index); std::is_neq(step)) return step;
    }
    for (auto const index : decltype(left.pairs)::indices()) {
        if (auto const step = left.pairs.at(index) <=> right.pairs.at(index); std::is_neq(step)) return step;
    }
    if (auto const step = left.ticket.raw() <=> right.ticket.raw(); std::is_neq(step)) return step;
    return left.address <=> right.address;
}

// The hand fold of the hash of THE HASH of foundation/core/Protocol.h.
constexpr std::uint64_t kClassSeed = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kArrayPrime = 0x100000001b3ULL;

[[nodiscard]] std::uint64_t mix(std::uint64_t word) noexcept { return ::foundation::reflect::fmix64(word); }

[[nodiscard]] std::uint64_t hand_hash(Sample const& sample) noexcept {
    std::uint64_t const header = mix(kClassSeed * kClassSeed ^ mix(static_cast<std::uint64_t>(sample.tag)));
    std::uint64_t pad = 0;
    for (std::uint8_t const byte : sample.pad.view()) {
        pad = pad * kArrayPrime ^ mix(byte);
    }
    std::uint64_t lanes = 0;
    for (std::uint16_t const lane : sample.lanes.view()) {
        lanes = lanes * kArrayPrime ^ mix(lane);
    }
    std::uint64_t pairs = 0;
    for (std::int32_t const pair : sample.pairs.view()) {
        pairs = pairs * kArrayPrime ^ mix(static_cast<std::uint64_t>(pair));
    }
    std::uint64_t const ticket = mix(kClassSeed * kClassSeed ^ mix(sample.ticket.raw()));
    FixedArray<std::uint64_t, 13> const parts{
        header,
        mix(sample.small),
        mix(static_cast<std::uint64_t>(sample.signed_small)),
        mix(sample.flag ? 1U : 0U),
        mix(static_cast<std::uint64_t>(sample.phase)),
        mix(pad),
        mix(sample.word),
        mix(static_cast<std::uint64_t>(sample.wide)),
        mix(bit_cast<std::uint32_t>(sample.ratio)),
        mix(bit_cast<std::uint64_t>(sample.scale)),
        mix(lanes),
        mix(pairs),
        ticket,
    };
    std::uint64_t state = kClassSeed;
    for (std::uint64_t const part : parts.view()) {
        state = state * kClassSeed ^ part;
    }
    return mix(state);
}

// The byte at an offset of an image.
[[nodiscard]] std::byte& byte_at(Image& image, std::size_t at) noexcept {
    return image.at(Index<encoded_size_v<Sample>>::check(at).expect("the offset is inside the image"));
}

// The hand image: each field in declaration order, the low byte first.
template <class Word>
void put_word(Image& image, std::size_t at, Word word) noexcept {
    for (std::size_t index = 0; index < sizeof(Word); ++index) {
        byte_at(image, at + index) = static_cast<std::byte>(static_cast<std::uint8_t>(word >> (8 * index)));
    }
}

[[nodiscard]] Image hand_image(Sample const& sample) noexcept {
    Image image{};
    put_word(image, kTagAt, static_cast<std::uint16_t>(sample.tag));
    put_word(image, kSmallAt, sample.small);
    put_word(image, kSignedSmallAt, static_cast<std::uint8_t>(sample.signed_small));
    put_word(image, kFlagAt, static_cast<std::uint8_t>(sample.flag ? 1 : 0));
    put_word(image, kPhaseAt, static_cast<std::uint8_t>(sample.phase));
    put_word(image, kPadAt, sample.pad.at(index<0>()));
    put_word(image, kPadAt + 1, sample.pad.at(index<1>()));
    put_word(image, kWordAt, sample.word);
    put_word(image, kWideAt, static_cast<std::uint64_t>(sample.wide));
    put_word(image, kRatioAt, bit_cast<std::uint32_t>(sample.ratio));
    put_word(image, kScaleAt, bit_cast<std::uint64_t>(sample.scale));
    for (auto const index : decltype(sample.lanes)::indices()) {
        put_word(image, kLanesAt + 2 * index.raw(), sample.lanes.at(index));
    }
    put_word(image, kPairsAt, static_cast<std::uint32_t>(sample.pairs.at(index<0>())));
    put_word(image, kPairsAt + 4, static_cast<std::uint32_t>(sample.pairs.at(index<1>())));
    put_word(image, kTicketAt, sample.ticket.raw());
    return image;
}

// The image of the sample.
[[nodiscard]] Image image_of(Sample const& sample) noexcept {
    Image image{};
    encode(sample, image.view());
    return image;
}

// The decode of an image that the test names.
[[nodiscard]] ::foundation::core::Result<Sample, DecodeFault> decode_of(Image const& image) noexcept {
    return decode<Sample>(image.view());
}

void test_equal_and_order_agree_with_the_hand_forms() {
    ::crucible::test::PhiloxStream stream{0x50524F544F434F4CULL};
    for (int round = 0; round < 4096; ++round) {
        Sample const left = random_sample(stream);
        Sample right = (stream.next_word() & 3U) == 0 ? left : random_sample(stream);
        if ((stream.next_word() & 7U) == 0) right.address = left.address + 1;
        CRUCIBLE_FATAL_INVARIANT(equal(left, right) == hand_equal(left, right));
        CRUCIBLE_FATAL_INVARIANT(order(left, right) == hand_order(left, right));
        CRUCIBLE_FATAL_INVARIANT(equal(left, right) == std::is_eq(order(left, right)));
        CRUCIBLE_FATAL_INVARIANT(order(right, left) == hand_order(right, left));
    }
}

void test_hash_agrees_with_the_hand_fold() {
    ::crucible::test::PhiloxStream stream{0x4841534846414D31ULL};
    for (int round = 0; round < 4096; ++round) {
        Sample const sample = random_sample(stream);
        CRUCIBLE_FATAL_INVARIANT(hash(sample).raw() == hand_hash(sample));
        // A local part has no place in the hash.
        Sample moved = sample;
        moved.address = ~sample.address;
        CRUCIBLE_FATAL_INVARIANT(hash(moved) == hash(sample));
    }
    // One pinned value: the fold reads values, so each target gives it.
    Sample pinned{};
    pinned.tag = -2;
    pinned.small = 0xAB;
    pinned.signed_small = -1;
    pinned.flag = true;
    pinned.phase = Phase::other;
    pinned.word = 0x12345678U;
    pinned.wide = -0x0123456789ABCDEFLL;
    pinned.ratio = -0.0f;
    pinned.scale = 1.5;
    pinned.lanes.at(index<1>()) = 0xCDEF;
    pinned.pairs.at(index<1>()) = -7;
    pinned.ticket = Ticket{42};
    CRUCIBLE_FATAL_INVARIANT(hash(pinned).raw() == hand_hash(pinned));
}

void test_encode_writes_the_hand_image() {
    ::crucible::test::PhiloxStream stream{0x494D414745494D31ULL};
    for (int round = 0; round < 4096; ++round) {
        Sample const sample = random_sample(stream);
        CRUCIBLE_FATAL_INVARIANT(image_of(sample) == hand_image(sample));
    }
}

void test_decode_inverts_encode() {
    ::crucible::test::PhiloxStream stream{0x524F554E44545249ULL};
    for (int round = 0; round < 4096; ++round) {
        Sample const sample = random_sample(stream);
        Sample expected = sample;
        expected.address = 0;
        bool const is_back = decode_of(image_of(sample))
                                 .match([&expected](Sample back) noexcept { return equal(back, expected); },
                                        [](DecodeFault) noexcept { return false; });
        CRUCIBLE_FATAL_INVARIANT(is_back);
    }
}

// The fault of an image, or a fault with an offset past each image when the
// decode gives a value.
[[nodiscard]] DecodeFault fault_of(Image const& image) noexcept {
    return decode_of(image).match([](Sample) noexcept { return DecodeFault{DecodeRefusal::bool_byte, 9999}; },
                                  [](DecodeFault fault) noexcept { return fault; });
}

void test_decode_refuses_each_byte_that_names_no_value() {
    Image const clean = image_of(Sample{});

    Image bad_bool = clean;
    byte_at(bad_bool, kFlagAt) = std::byte{2};
    CRUCIBLE_FATAL_INVARIANT((fault_of(bad_bool) == DecodeFault{DecodeRefusal::bool_byte, kFlagAt}));

    Image bad_phase = clean;
    byte_at(bad_phase, kPhaseAt) = std::byte{3};
    CRUCIBLE_FATAL_INVARIANT((fault_of(bad_phase) == DecodeFault{DecodeRefusal::enumerator, kPhaseAt}));

    Image bad_pad = clean;
    byte_at(bad_pad, kPadAt + 1) = std::byte{1};
    CRUCIBLE_FATAL_INVARIANT((fault_of(bad_pad) == DecodeFault{DecodeRefusal::pad_byte, kPadAt}));

    // The first fault in the order of the parts wins.
    Image two_faults = bad_pad;
    byte_at(two_faults, kFlagAt) = std::byte{0xFF};
    CRUCIBLE_FATAL_INVARIANT((fault_of(two_faults) == DecodeFault{DecodeRefusal::bool_byte, kFlagAt}));

    // Each other value of the gap is refused too, and each enumerator is read.
    for (unsigned raw = 0; raw < 256; ++raw) {
        Image image = clean;
        byte_at(image, kPhaseAt) = static_cast<std::byte>(raw);
        bool const names_value = raw == 0 || raw == 1 || raw == 7;
        CRUCIBLE_FATAL_INVARIANT(decode_of(image).is_ok() == names_value);
    }
}

void test_format_names_each_part() {
    Sample sample{};
    sample.tag = -3;
    sample.flag = true;
    sample.phase = Phase::other;
    sample.ratio = 0.5f;
    sample.lanes.at(index<2>()) = 9;
    sample.ticket = Ticket{7};
    sample.address = 1;
    FixedText<512> out{};
    TextView const expected{
        "{tag = -3, small = 0, signed_small = 0, flag = true, phase = other, word = 0, wide = 0, ratio = 0.5, "
        "scale = 0, lanes = [0, 0, 9], pairs = [0, 0], ticket = {word_ = 7}}"};
    bool const is_expected = format(out, sample)
                                 .match([&expected](TextView text) noexcept { return text == expected; },
                                        [](::foundation::core::Full<char>) noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_expected);

    // A short buffer gives Full<char>.
    FixedText<16> short_out{};
    CRUCIBLE_FATAL_INVARIANT(format(short_out, sample).is_err());
}

// The float rule over the whole walk: +0 and -0 differ, a NaN with one set
// of bits equals itself, and the hash and the image tell the two zeros
// apart.
void test_float_rule() {
    Sample positive{};
    Sample negative{};
    negative.scale = -0.0;
    CRUCIBLE_FATAL_INVARIANT(!equal(positive, negative) && std::is_gt(order(positive, negative)));
    CRUCIBLE_FATAL_INVARIANT(hash(positive) != hash(negative) && image_of(positive) != image_of(negative));
    Sample nan_one{};
    nan_one.ratio = ::foundation::core::quiet_nan_of<float>;
    Sample const nan_two = nan_one;
    CRUCIBLE_FATAL_INVARIANT(equal(nan_one, nan_two) && hash(nan_one) == hash(nan_two));
}

// ── A C array part and a pointer part ──────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the walk of the Protocol family over a C array part and a pointer part")

// A C array part compares, orders, hashes and encodes element by element,
// as a FixedArray part of the same elements does.
struct[[= derive_protocol{}]] HoldsCArray {
    std::uint16_t lanes[3] = {};
};

struct[[= derive_protocol{}]] HoldsFixedArray {
    FixedArray<std::uint16_t, 3> lanes{};
};

// A pointer compares as an address, and a local pointer has no place in the
// hash or the image.
struct[[= derive_protocol{}]] HoldsPointer {
    std::uint32_t word = 0;
    [[= local_part{}]] int const* target = nullptr;
};

void test_c_array_and_pointer_parts() {
    static_assert(encoded_size_v<HoldsCArray> == 6 && encoded_size_v<HoldsFixedArray> == 6);
    static_assert(::foundation::core::Encodable<HoldsPointer> && encoded_size_v<HoldsPointer> == 4);
    static_assert(::foundation::core::Equatable<HoldsPointer> && !::foundation::core::Orderable<HoldsPointer>);

    ::crucible::test::PhiloxStream stream{0x4341525241595054ULL};
    for (int round = 0; round < 1024; ++round) {
        HoldsCArray c_array{};
        HoldsFixedArray fixed_array{};
        for (std::size_t lane = 0; lane < 3; ++lane) {
            c_array.lanes[lane] = static_cast<std::uint16_t>(stream.next_word());
            fixed_array.lanes.at(Index<3>::check(lane).expect("three lanes")) = c_array.lanes[lane];
        }
        CRUCIBLE_FATAL_INVARIANT(hash(c_array).raw() == hash(fixed_array).raw());
        FixedArray<std::byte, 6> c_image{};
        FixedArray<std::byte, 6> fixed_image{};
        encode(c_array, c_image.view());
        encode(fixed_array, fixed_image.view());
        CRUCIBLE_FATAL_INVARIANT(c_image == fixed_image);
        HoldsCArray other = c_array;
        other.lanes[2] = static_cast<std::uint16_t>(other.lanes[2] + 1U);
        CRUCIBLE_FATAL_INVARIANT(!equal(c_array, other) && equal(c_array, c_array));
        CRUCIBLE_FATAL_INVARIANT(std::is_lt(order(c_array, other)) == (c_array.lanes[2] < other.lanes[2]));
        FixedArray<std::byte, 6> const& read = c_image;
        bool const is_back = decode<HoldsCArray>(read.view())
                                 .match([&c_array](HoldsCArray back) noexcept { return equal(back, c_array); },
                                        [](DecodeFault) noexcept { return false; });
        CRUCIBLE_FATAL_INVARIANT(is_back);
    }

    int const first = 1;
    int const second = 2;
    HoldsPointer const at_first{7, &first};
    HoldsPointer const at_second{7, &second};
    CRUCIBLE_FATAL_INVARIANT(equal(at_first, at_first) && !equal(at_first, at_second));
    CRUCIBLE_FATAL_INVARIANT(hash(at_first) == hash(at_second) && hash(at_first) == hash(HoldsPointer{7, nullptr}));
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    test_equal_and_order_agree_with_the_hand_forms();
    test_hash_agrees_with_the_hand_fold();
    test_encode_writes_the_hand_image();
    test_decode_inverts_encode();
    test_decode_refuses_each_byte_that_names_no_value();
    test_format_names_each_part();
    test_float_rule();
    test_c_array_and_pointer_parts();
    return 0;
}
