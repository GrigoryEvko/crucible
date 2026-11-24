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
#include <utility>

namespace {

using ::foundation::core::bit_cast;
using ::foundation::core::decode;
using ::foundation::core::DecodeFault;
using ::foundation::core::DecodeRefusal;
using ::foundation::core::derive_protocol;
using ::foundation::core::Digest;
using ::foundation::core::encode;
using ::foundation::core::encoded_size_v;
using ::foundation::core::enum_from;
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
        auto const lane = static_cast<std::uint16_t>(stream.next_word());
        sample.lanes.set(index, lane);
    }
    for (auto const index : decltype(sample.pairs)::indices()) {
        auto const pair = static_cast<std::int32_t>(stream.next_word());
        sample.pairs.set(index, pair);
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

// The hand fold of the hash of foundation/core/Protocol.h over the bytes of
// an image: blocks of 16 bytes, the last one filled with zero bytes, each
// block as two little-endian words.
constexpr std::uint64_t kFoldSeed = 0x243f6a8885a308d3ULL;
constexpr std::uint64_t kStartSeed = 0x13198a2e03707344ULL;

// The hash of the pinned Sample of test_hash_agrees_with_the_hand_fold, from
// a fold of its image by a Python script outside the tree: the fold is
// integer arithmetic on the bytes of the image, so each target gives it.
constexpr std::uint64_t kPinnedSampleHash = 0x00ed05457e277e0cULL;

[[nodiscard]] std::uint64_t folded_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    ::foundation::core::WideProduct<std::uint64_t> const product = ::foundation::core::widening_mul(left, right);
    return product.low ^ product.high;
}

template <std::size_t N>
[[nodiscard]] std::uint64_t hand_hash_of_image(FixedArray<std::byte, N> const& image) noexcept {
    std::uint64_t accumulator = kStartSeed;
    for (std::size_t first = 0; first < N; first += 16) {
        std::uint64_t low = 0;
        std::uint64_t high = 0;
        for (std::size_t lane = 0; lane < 16; ++lane) {
            std::size_t const at = first + lane;
            std::uint64_t const byte =
                at < N ? static_cast<std::uint8_t>(image.at(Index<N>::check(at).expect("the byte is in the image")))
                       : 0;
            if (lane < 8) {
                low |= byte << (8 * lane);
            } else {
                high |= byte << (8 * (lane - 8));
            }
        }
        accumulator = folded_multiply(low ^ accumulator, high ^ kFoldSeed);
    }
    return accumulator;
}

// Writes the byte at an offset of an image.
void put_byte(Image& image, std::size_t at, std::byte byte) noexcept {
    image.set(Index<encoded_size_v<Sample>>::check(at).expect("the offset is inside the image"), byte);
}

// The hand image: each field in declaration order, the low byte first.
template <class Word>
void put_word(Image& image, std::size_t at, Word word) noexcept {
    for (std::size_t index = 0; index < sizeof(Word); ++index) {
        put_byte(image, at + index, static_cast<std::byte>(static_cast<std::uint8_t>(word >> (8 * index))));
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
    image.with_mut([&sample](auto run) noexcept { encode(sample, std::move(run)); });
    return image;
}

// The hand hash of a Sample: the hand fold of its hand image.
[[nodiscard]] std::uint64_t hand_hash(Sample const& sample) noexcept { return hand_hash_of_image(hand_image(sample)); }

// The image of a value of an Encodable type, by encode.
template <class T>
[[nodiscard]] FixedArray<std::byte, encoded_size_v<T>> encoded(T const& value) noexcept {
    FixedArray<std::byte, encoded_size_v<T>> image{};
    image.with_mut([&value](auto run) noexcept { encode(value, std::move(run)); });
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
    pinned.lanes.set(index<1>(), 0xCDEF);
    pinned.pairs.set(index<1>(), -7);
    pinned.ticket = Ticket{42};
    CRUCIBLE_FATAL_INVARIANT(hash(pinned).raw() == hand_hash(pinned));
    CRUCIBLE_FATAL_INVARIANT(hash(pinned).raw() == kPinnedSampleHash);
}

// ── The paths of the fold over an array ────────────────────────────────

// A pair of two widths: an element of a class for the fold of a long array.
struct[[= derive_protocol{}]] Pair {
    std::uint16_t narrow = 0;
    std::uint8_t tiny = 0;
};

// A long array of leaves that starts at a multiple of the size of a leaf:
// the fold of whole blocks in a loop, with a head before the first block and
// a tail after the last one.
struct[[= derive_protocol{}]] AlignedBytes {
    std::uint64_t lead = 0;
    FixedArray<std::uint8_t, 50> bytes{};
};

struct[[= derive_protocol{}]] AlignedLanes {
    std::uint16_t lead = 0;
    FixedArray<std::uint16_t, 37> lanes{};
};

// A long array of leaves that does not start at a multiple of the size of a
// leaf, so a leaf crosses the end of a block: the fold of the bytes of each
// element.
struct[[= derive_protocol{}]] ShiftedWords {
    std::uint8_t lead = 0;
    FixedArray<std::uint32_t, 40> words{};
    std::uint8_t last = 0;
};

struct[[= derive_protocol{}]] ShiftedWide {
    std::uint32_t lead = 0;
    FixedArray<std::uint64_t, 20> wide{};
};

// A long array of classes: the fold of the bytes of each element.
struct[[= derive_protocol{}]] ManyPairs {
    std::uint8_t lead = 0;
    FixedArray<Pair, 18> pairs{};
};

// A short array of classes and a leaf that crosses the end of a block.
struct[[= derive_protocol{}]] FewPairs {
    std::uint32_t lead = 0;
    FixedArray<Pair, 5> pairs{};
    std::uint64_t crossing = 0;
};

// An image of no byte, of one full block, and of one byte more.
struct[[= derive_protocol{}]] Empty {};

struct[[= derive_protocol{}]] OneBlock {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

struct[[= derive_protocol{}]] OneBlockAndByte {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    std::uint8_t more = 0;
};

// Each leaf of a value from the stream: the bits of its unsigned word.
template <class Leaf>
[[nodiscard]] Leaf random_leaf(::crucible::test::PhiloxStream& stream) noexcept {
    return static_cast<Leaf>(stream.next_wide());
}

template <class Leaf, std::size_t N>
void fill(FixedArray<Leaf, N>& array, ::crucible::test::PhiloxStream& stream) noexcept {
    array.with_mut([&stream](auto run) noexcept {
        for (Leaf& leaf : run) {
            leaf = random_leaf<Leaf>(stream);
        }
    });
}

template <std::size_t N>
void fill(FixedArray<Pair, N>& array, ::crucible::test::PhiloxStream& stream) noexcept {
    array.with_mut([&stream](auto run) noexcept {
        for (Pair& pair : run) {
            pair.narrow = random_leaf<std::uint16_t>(stream);
            pair.tiny = random_leaf<std::uint8_t>(stream);
        }
    });
}

// The hash of the value is the hand fold of its image, and each byte of the
// image takes part.
template <class T>
void expect_hash_of_image(T const& value) noexcept {
    auto const image = encoded(value);
    CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_hash_of_image(image));
}

void test_hash_of_each_array_path() {
    static_assert(encoded_size_v<AlignedBytes> == 58 && encoded_size_v<AlignedLanes> == 76);
    static_assert(encoded_size_v<ShiftedWords> == 162 && encoded_size_v<ShiftedWide> == 164);
    static_assert(encoded_size_v<ManyPairs> == 55 && encoded_size_v<FewPairs> == 27);
    static_assert(encoded_size_v<Empty> == 0 && encoded_size_v<OneBlock> == 16
                  && encoded_size_v<OneBlockAndByte> == 17);

    ::crucible::test::PhiloxStream stream{0x4841534850415448ULL};
    for (int round = 0; round < 512; ++round) {
        AlignedBytes aligned_bytes{};
        aligned_bytes.lead = random_leaf<std::uint64_t>(stream);
        fill(aligned_bytes.bytes, stream);
        expect_hash_of_image(aligned_bytes);

        AlignedLanes aligned_lanes{};
        aligned_lanes.lead = random_leaf<std::uint16_t>(stream);
        fill(aligned_lanes.lanes, stream);
        expect_hash_of_image(aligned_lanes);

        ShiftedWords shifted_words{};
        shifted_words.lead = random_leaf<std::uint8_t>(stream);
        fill(shifted_words.words, stream);
        shifted_words.last = random_leaf<std::uint8_t>(stream);
        expect_hash_of_image(shifted_words);

        ShiftedWide shifted_wide{};
        shifted_wide.lead = random_leaf<std::uint32_t>(stream);
        fill(shifted_wide.wide, stream);
        expect_hash_of_image(shifted_wide);

        ManyPairs many_pairs{};
        many_pairs.lead = random_leaf<std::uint8_t>(stream);
        fill(many_pairs.pairs, stream);
        expect_hash_of_image(many_pairs);

        FewPairs few_pairs{};
        few_pairs.lead = random_leaf<std::uint32_t>(stream);
        fill(few_pairs.pairs, stream);
        few_pairs.crossing = random_leaf<std::uint64_t>(stream);
        expect_hash_of_image(few_pairs);

        OneBlock one_block{random_leaf<std::uint64_t>(stream), random_leaf<std::uint64_t>(stream)};
        expect_hash_of_image(one_block);

        OneBlockAndByte one_more{random_leaf<std::uint64_t>(stream), random_leaf<std::uint64_t>(stream),
                                 random_leaf<std::uint8_t>(stream)};
        expect_hash_of_image(one_more);
        // A byte past one full block gives a second fold, so the zero byte
        // after a block differs from the block alone.
        OneBlockAndByte const zero_more{one_block.low, one_block.high, 0};
        CRUCIBLE_FATAL_INVARIANT(hash(zero_more).raw() != hash(one_block).raw());
    }
    // An image of no byte folds no block.
    CRUCIBLE_FATAL_INVARIANT(hash(Empty{}).raw() == kStartSeed);
}

// ── The hash of a leaf that is no class ────────────────────────────────

// The image of a leaf is its bytes in little-endian order, which are its
// bytes on each target of the tree.
template <class Leaf>
[[nodiscard]] FixedArray<std::byte, sizeof(Leaf)> image_of_leaf(Leaf leaf) noexcept {
    return bit_cast<FixedArray<std::byte, sizeof(Leaf)>>(leaf);
}

// A leaf hashes as the image of its bytes, as a class of one part of the
// leaf does.
struct[[= derive_protocol{}]] HoldsWord {
    std::uint32_t word = 0;
};

void test_hash_of_a_leaf() {
    static_assert(::foundation::core::Hashable<std::uint32_t> && ::foundation::core::Hashable<Phase>);
    static_assert(::foundation::core::Hashable<bool> && ::foundation::core::Hashable<double>);
    static_assert(!::foundation::core::Hashable<int const*> && !::foundation::core::Hashable<std::uint32_t[2]>);
    static_assert(!::foundation::core::Hashable<long double>);
    static_assert(std::is_same_v<decltype(hash(std::uint32_t{1})), Digest<std::uint32_t>>);

    ::crucible::test::PhiloxStream stream{0x4C45414648415348ULL};
    for (int round = 0; round < 4096; ++round) {
        std::uint32_t const word = stream.next_word();
        CRUCIBLE_FATAL_INVARIANT(hash(word).raw() == hand_hash_of_image(image_of_leaf(word)));
        CRUCIBLE_FATAL_INVARIANT(hash(word).raw() == hash(HoldsWord{word}).raw());
        std::int64_t const wide = static_cast<std::int64_t>(stream.next_wide());
        CRUCIBLE_FATAL_INVARIANT(hash(wide).raw() == hand_hash_of_image(image_of_leaf(wide)));
    }
    // An enumeration hashes as its underlying integer, a bool as one byte of
    // 0 or 1, and a float by its bits, so the two zeros differ.
    CRUCIBLE_FATAL_INVARIANT(hash(Phase::other).raw() == hash(std::uint8_t{7}).raw());
    CRUCIBLE_FATAL_INVARIANT(hash(true).raw() == hash(std::uint8_t{1}).raw());
    CRUCIBLE_FATAL_INVARIANT(hash(0.0).raw() != hash(-0.0).raw());
    CRUCIBLE_FATAL_INVARIANT(hash(1.5f).raw() == hash(bit_cast<std::uint32_t>(1.5f)).raw());
}

// ── foldhash-f ──────────────────────────────────────────────────────────

// The integer path of foldhash-f of Orson Peters with the seeds of the hash:
// each integer goes into a sponge of 128 bits at the count of bits that the
// sponge holds, and a sponge that the next integer would pass folds first.
class Foldhash final {
public:
    void write(std::uint64_t value, unsigned bits) noexcept {
        if (sponge_bits_ + bits > 128) {
            accumulator_ = folded_multiply(sponge_low_ ^ accumulator_, sponge_high_ ^ kFoldSeed);
            sponge_low_ = value;
            sponge_high_ = 0;
            sponge_bits_ = bits;
            return;
        }
        if (sponge_bits_ < 64) {
            sponge_low_ |= value << sponge_bits_;
            if (sponge_bits_ != 0 && sponge_bits_ + bits > 64) sponge_high_ |= value >> (64 - sponge_bits_);
        } else {
            sponge_high_ |= value << (sponge_bits_ - 64);
        }
        sponge_bits_ += bits;
    }

    [[nodiscard]] std::uint64_t finish() const noexcept {
        if (sponge_bits_ == 0) return accumulator_;
        return folded_multiply(sponge_low_ ^ accumulator_, sponge_high_ ^ kFoldSeed);
    }

private:
    std::uint64_t accumulator_ = kStartSeed;
    std::uint64_t sponge_low_ = 0;
    std::uint64_t sponge_high_ = 0;
    unsigned sponge_bits_ = 0;
};

// A record whose leaves fill whole blocks: 4 + 2 + 2 + 8 bytes, then 8 more.
struct[[= derive_protocol{}]] Filled {
    std::uint32_t id = 0;
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;
    std::int64_t payload = 0;
    std::uint64_t extra = 0;
};

// The hash of a record whose leaves fill whole blocks is foldhash-f of its
// fields, in declaration order.
void test_hash_is_foldhash_on_whole_blocks() {
    ::crucible::test::PhiloxStream stream{0x464F4C4448415348ULL};
    for (int round = 0; round < 4096; ++round) {
        Filled const filled{random_leaf<std::uint32_t>(stream), random_leaf<std::uint16_t>(stream),
                            random_leaf<std::uint16_t>(stream), random_leaf<std::int64_t>(stream),
                            random_leaf<std::uint64_t>(stream)};
        Foldhash reference{};
        reference.write(filled.id, 32);
        reference.write(filled.kind, 16);
        reference.write(filled.flags, 16);
        reference.write(static_cast<std::uint64_t>(filled.payload), 64);
        reference.write(filled.extra, 64);
        CRUCIBLE_FATAL_INVARIANT(hash(filled).raw() == reference.finish());
    }
}

// ── The hash of a run ───────────────────────────────────────────────────

// An element of 4 bytes: four of them fill one block.
struct[[= derive_protocol{}]] Edge {
    std::uint16_t from = 0;
    std::uint16_t to = 0;
};

// The hand fold of a run: the images of the elements one after the other,
// the last block filled with zero bytes, and then one block with the count.
template <class T, std::size_t N>
[[nodiscard]] std::uint64_t hand_hash_of_run(FixedArray<T, N> const& elements, std::size_t count) noexcept {
    constexpr std::size_t step = encoded_size_v<T>;
    FixedArray<std::byte, N * step> bytes{};
    for (std::size_t index = 0; index < count; ++index) {
        auto const image = encoded(elements.at(Index<N>::check(index).expect("the index is below the count")));
        for (std::size_t at = 0; at < step; ++at) {
            std::byte const byte = image.at(Index<step>::check(at).expect("the byte is in the image"));
            bytes.set(Index<N * step>::check(index * step + at).expect("the byte is in the run"), byte);
        }
    }
    std::uint64_t accumulator = kStartSeed;
    std::size_t const total = count * step;
    for (std::size_t first = 0; first < total; first += 16) {
        std::uint64_t low = 0;
        std::uint64_t high = 0;
        for (std::size_t lane = 0; lane < 16 && first + lane < total; ++lane) {
            std::uint64_t const byte = static_cast<std::uint8_t>(
                bytes.at(Index<N * step>::check(first + lane).expect("the byte is in the run")));
            if (lane < 8) {
                low |= byte << (8 * lane);
            } else {
                high |= byte << (8 * (lane - 8));
            }
        }
        accumulator = folded_multiply(low ^ accumulator, high ^ kFoldSeed);
    }
    return folded_multiply(static_cast<std::uint64_t>(count) ^ accumulator, kFoldSeed);
}

// The hash of the first count elements, through a View of them.
template <class T, std::size_t N>
[[nodiscard]] std::uint64_t hash_of_run(FixedArray<T, N> const& elements, std::size_t count) noexcept {
    ::foundation::core::View<T const> const run = elements.view().window(0, count).expect("the count is at most N");
    return hash(run).raw();
}

void test_hash_of_a_run() {
    ::crucible::test::PhiloxStream stream{0x52554E4841534831ULL};
    FixedArray<Pair, 40> pairs{};
    FixedArray<Edge, 40> edges{};
    FixedArray<OneBlock, 6> blocks{};
    for (int round = 0; round < 64; ++round) {
        fill(pairs, stream);
        edges.with_mut([&stream](auto run) noexcept {
            for (Edge& edge : run) {
                edge = Edge{random_leaf<std::uint16_t>(stream), random_leaf<std::uint16_t>(stream)};
            }
        });
        blocks.with_mut([&stream](auto run) noexcept {
            for (OneBlock& block : run) {
                block = OneBlock{random_leaf<std::uint64_t>(stream), random_leaf<std::uint64_t>(stream)};
            }
        });
        for (std::size_t count = 0; count <= 40; ++count) {
            CRUCIBLE_FATAL_INVARIANT(hash_of_run(pairs, count) == hand_hash_of_run(pairs, count));
            CRUCIBLE_FATAL_INVARIANT(hash_of_run(edges, count) == hand_hash_of_run(edges, count));
        }
        for (std::size_t count = 0; count <= 6; ++count) {
            CRUCIBLE_FATAL_INVARIANT(hash_of_run(blocks, count) == hand_hash_of_run(blocks, count));
        }
    }
    // A zero element after a run gives another hash, and the order of the
    // elements takes part.
    FixedArray<Edge, 2> const one_and_zero{Edge{1, 2}, Edge{0, 0}};
    FixedArray<Edge, 2> const two{Edge{1, 2}, Edge{3, 4}};
    FixedArray<Edge, 2> const swapped{Edge{3, 4}, Edge{1, 2}};
    CRUCIBLE_FATAL_INVARIANT(hash_of_run(one_and_zero, 1) != hash_of_run(one_and_zero, 2));
    CRUCIBLE_FATAL_INVARIANT(hash_of_run(two, 2) != hash_of_run(swapped, 2));
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
    put_byte(bad_bool, kFlagAt, std::byte{2});
    CRUCIBLE_FATAL_INVARIANT((fault_of(bad_bool) == DecodeFault{DecodeRefusal::bool_byte, kFlagAt}));

    Image bad_phase = clean;
    put_byte(bad_phase, kPhaseAt, std::byte{3});
    CRUCIBLE_FATAL_INVARIANT((fault_of(bad_phase) == DecodeFault{DecodeRefusal::enumerator, kPhaseAt}));

    Image bad_pad = clean;
    put_byte(bad_pad, kPadAt + 1, std::byte{1});
    CRUCIBLE_FATAL_INVARIANT((fault_of(bad_pad) == DecodeFault{DecodeRefusal::pad_byte, kPadAt}));

    // The first fault in the order of the parts wins.
    Image two_faults = bad_pad;
    put_byte(two_faults, kFlagAt, std::byte{0xFF});
    CRUCIBLE_FATAL_INVARIANT((fault_of(two_faults) == DecodeFault{DecodeRefusal::bool_byte, kFlagAt}));

    // Each other value of the gap is refused too, and each enumerator is read.
    for (unsigned raw = 0; raw < 256; ++raw) {
        Image image = clean;
        put_byte(image, kPhaseAt, static_cast<std::byte>(raw));
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
    sample.lanes.set(index<2>(), 9);
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

// A defaulted == of a C array of a float compares each element by IEEE 754,
// so the class is no Equatable type.  Under the mark ieee_equality the same
// member makes a PartialEquatable class.
struct HoldsFloatCArray {
    float lanes[2] = {};
    friend constexpr bool operator==(HoldsFloatCArray const&, HoldsFloatCArray const&) noexcept = default;
};

struct[[= ::foundation::core::ieee_equality{}]] StatesFloatCArray {
    float lanes[2] = {};
    friend constexpr bool operator==(StatesFloatCArray const&, StatesFloatCArray const&) noexcept = default;
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
    static_assert(!::foundation::core::Equatable<HoldsFloatCArray> && ::foundation::core::Equatable<HoldsCArray>);
    static_assert(!::foundation::core::Equatable<StatesFloatCArray>
                  && ::foundation::core::PartialEquatable<StatesFloatCArray>);

    ::crucible::test::PhiloxStream stream{0x4341525241595054ULL};
    for (int round = 0; round < 1024; ++round) {
        HoldsCArray c_array{};
        HoldsFixedArray fixed_array{};
        for (std::size_t lane = 0; lane < 3; ++lane) {
            c_array.lanes[lane] = static_cast<std::uint16_t>(stream.next_word());
            std::uint16_t const lane_value = c_array.lanes[lane];
            fixed_array.lanes.set(Index<3>::check(lane).expect("three lanes"), lane_value);
        }
        CRUCIBLE_FATAL_INVARIANT(hash(c_array).raw() == hash(fixed_array).raw());
        FixedArray<std::byte, 6> c_image{};
        FixedArray<std::byte, 6> fixed_image{};
        c_image.with_mut([&c_array](auto run) noexcept { encode(c_array, std::move(run)); });
        fixed_image.with_mut([&fixed_array](auto run) noexcept { encode(fixed_array, std::move(run)); });
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

// ── The sums ─────────────────────────────────────────────────────────────

enum class SumFault : std::uint8_t {
    first = 1,
    second = 2
};

using MaybeWord = ::foundation::core::Option<std::uint32_t>;
using Outcome = ::foundation::core::Result<std::uint16_t, SumFault>;
using Either = ::foundation::core::OneOf<std::uint8_t, std::uint64_t>;

// The image: head at 0, the Option at 2 (its payload at 3 to 6), the Result
// at 7 (its room at 8 and 9), and the OneOf at 10 (its room at 11 to 18).
struct[[= derive_protocol{}]] WithSums {
    std::uint16_t head = 0;
    MaybeWord maybe{};
    Outcome outcome{std::uint16_t{0}};
    Either either{std::uint8_t{0}};
};

using SumImage = FixedArray<std::byte, encoded_size_v<WithSums>>;

[[nodiscard]] WithSums sample_with_sums() noexcept {
    WithSums value{};
    value.head = 0x0102;
    value.maybe = MaybeWord::some(0x0A0B0C0DU);
    value.outcome = Outcome{::foundation::core::Err<SumFault>{SumFault::second}};
    value.either = Either{std::uint64_t{0x1122334455667788ULL}};
    return value;
}

[[nodiscard]] DecodeFault sum_fault_of(SumImage const& image) noexcept {
    return decode<WithSums>(image.view())
        .match([](WithSums) noexcept { return DecodeFault{DecodeRefusal::bool_byte, 9999}; },
               [](DecodeFault fault) noexcept { return fault; });
}

// True when decode of the image of value gives a value equal to it.
template <class T>
[[nodiscard]] bool decodes_back(T const& value) noexcept {
    FixedArray<std::byte, encoded_size_v<T>> const image = encoded(value);
    return decode<T>(image.view())
        .match([&value](T back) noexcept { return equal(back, value); }, [](DecodeFault) noexcept { return false; });
}

// The image of each sum is its tag and then its active alternative, with
// zero bytes after it.
void test_sums_encode_their_tag_and_alternative() {
    static_assert(encoded_size_v<WithSums> == 19);
    SumImage const image = encoded(sample_with_sums());
    constexpr FixedArray<std::uint8_t, 19> expected{0x02, 0x01, 0x01, 0x0D, 0x0C, 0x0B, 0x0A, 0x01, 0x02, 0x00,
                                                    0x01, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    std::size_t at = 0;
    for (std::byte const byte : image) {
        CRUCIBLE_FATAL_INVARIANT(static_cast<std::uint8_t>(byte)
                                 == expected.at(Index<19>::check(at).expect("19 bytes")));
        ++at;
    }

    WithSums empty{};
    SumImage const empty_image = encoded(empty);
    for (std::byte const byte : empty_image) {
        CRUCIBLE_FATAL_INVARIANT(byte == std::byte{0});
    }
}

// decode gives back each alternative of each sum, and the hash folds the
// image as the hand fold does.
void test_sums_decode_back_and_hash_their_image() {
    WithSums value = sample_with_sums();
    CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
    CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_hash_of_image(encoded(value)));
    value.maybe = MaybeWord{::foundation::core::none};
    value.outcome = Outcome{std::uint16_t{0xBEEF}};
    value.either = Either{std::uint8_t{0x5A}};
    CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
    CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_hash_of_image(encoded(value)));
    CRUCIBLE_FATAL_INVARIANT(hash(value) != hash(sample_with_sums()));
    CRUCIBLE_FATAL_INVARIANT(decodes_back(WithSums{}));
}

// Random values of each alternative decode back, and the hash of each is the
// hand fold of its image.
void test_sums_round_trip_random_values() {
    ::crucible::test::PhiloxStream stream{0x53554D53524F554EULL};
    for (int round = 0; round < 4096; ++round) {
        std::uint64_t const word = stream.next_word();
        WithSums value{};
        value.head = static_cast<std::uint16_t>(word);
        bool const has_payload = ((word >> 16) & 1U) != 0;
        bool const is_error = ((word >> 17) & 1U) != 0;
        bool const is_first_fault = ((word >> 18) & 1U) != 0;
        bool const is_wide = ((word >> 19) & 1U) != 0;
        if (has_payload) value.maybe = MaybeWord::some(static_cast<std::uint32_t>(word >> 32));
        SumFault const fault = is_first_fault ? SumFault::first : SumFault::second;
        value.outcome = is_error ? Outcome{::foundation::core::Err<SumFault>{fault}}
                                 : Outcome{static_cast<std::uint16_t>(word >> 20)};
        std::uint64_t const wide = stream.next_word();
        value.either = is_wide ? Either{wide} : Either{static_cast<std::uint8_t>(wide)};
        CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
        CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_hash_of_image(encoded(value)));
    }
}

struct[[= derive_protocol{}]] WithFloatSums {
    ::foundation::core::Option<float> maybe{};
    ::foundation::core::Result<double, SumFault> outcome{0.0};
};

// The float rule holds in a sum: equal of two values agrees with the
// equality of their images, so +0 and -0 differ, and a NaN equals itself.
void test_sums_obey_the_float_rule() {
    WithFloatSums positive{};
    positive.maybe = ::foundation::core::Option<float>::some(0.0f);
    WithFloatSums negative{};
    negative.maybe = ::foundation::core::Option<float>::some(-0.0f);
    negative.outcome = ::foundation::core::Result<double, SumFault>{-0.0};
    WithFloatSums not_a_number{};
    not_a_number.maybe = ::foundation::core::Option<float>::some(bit_cast<float>(std::uint32_t{0x7FC00001U}));
    not_a_number.outcome = ::foundation::core::Result<double, SumFault>{bit_cast<double>(0x7FF8000000000001ULL)};
    FixedArray<WithFloatSums, 4> const values{WithFloatSums{}, positive, negative, not_a_number};
    for (WithFloatSums const& left : values) {
        for (WithFloatSums const& right : values) {
            bool const has_equal_images = encoded(left) == encoded(right);
            CRUCIBLE_FATAL_INVARIANT(equal(left, right) == has_equal_images);
            CRUCIBLE_FATAL_INVARIANT((hash(left) == hash(right)) == has_equal_images);
        }
        CRUCIBLE_FATAL_INVARIANT(decodes_back(left));
    }
    CRUCIBLE_FATAL_INVARIANT(!equal(positive, negative) && equal(not_a_number, not_a_number));
}

// decode refuses a tag of no alternative and a byte after the active
// alternative that is not zero, at the offset of the byte.
void test_sums_refuse_each_byte_that_names_no_value() {
    SumImage const clean = encoded(sample_with_sums());

    SumImage bad_option_tag = clean;
    bad_option_tag.set(index<2>(), std::byte{2});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(bad_option_tag) == DecodeFault{DecodeRefusal::sum_tag, 2}));

    SumImage none_with_payload = encoded(WithSums{});
    none_with_payload.set(index<4>(), std::byte{1});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(none_with_payload) == DecodeFault{DecodeRefusal::tail_byte, 3}));

    SumImage bad_result_tag = clean;
    bad_result_tag.set(index<7>(), std::byte{2});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(bad_result_tag) == DecodeFault{DecodeRefusal::sum_tag, 7}));

    SumImage error_with_tail = clean;
    error_with_tail.set(index<9>(), std::byte{1});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(error_with_tail) == DecodeFault{DecodeRefusal::tail_byte, 9}));

    SumImage bad_error = clean;
    bad_error.set(index<8>(), std::byte{3});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(bad_error) == DecodeFault{DecodeRefusal::enumerator, 8}));

    SumImage bad_one_of_tag = clean;
    bad_one_of_tag.set(index<10>(), std::byte{2});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(bad_one_of_tag) == DecodeFault{DecodeRefusal::sum_tag, 10}));

    SumImage narrow_with_tail = encoded(WithSums{});
    narrow_with_tail.set(index<13>(), std::byte{1});
    CRUCIBLE_FATAL_INVARIANT((sum_fault_of(narrow_with_tail) == DecodeFault{DecodeRefusal::tail_byte, 12}));
}

void test_sums_format_their_alternative() {
    FixedText<256> out{};
    TextView const expected{
        "{head = 258, maybe = some(168496141), outcome = err(second), either = #1(1234605616436508552)}"};
    bool const is_expected = format(out, sample_with_sums())
                                 .match([&expected](TextView text) noexcept { return text == expected; },
                                        [](::foundation::core::Full<char>) noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_expected);

    FixedText<256> empty_out{};
    TextView const empty_expected{"{head = 0, maybe = none, outcome = ok(0), either = #0(0)}"};
    bool const is_empty_expected =
        format(empty_out, WithSums{})
            .match([&empty_expected](TextView text) noexcept { return text == empty_expected; },
                   [](::foundation::core::Full<char>) noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_empty_expected);
}

// ── The bounded lists ────────────────────────────────────────────────────

using Words = ::foundation::core::FixedList<std::uint16_t, 3>;
using Name = FixedText<5>;

// The image: head at 0, the list of words at 1 (its rooms at 2 to 7), and the
// text at 8 (its rooms at 9 to 13).
struct[[= derive_protocol{}]] WithLists {
    std::uint8_t head = 0;
    Words words{};
    Name name{};
};

using ListImage = FixedArray<std::byte, encoded_size_v<WithLists>>;

[[nodiscard]] WithLists sample_with_lists() noexcept {
    WithLists value{};
    value.head = 7;
    bool const is_built = value.words.append(std::uint16_t{0x0A0B}).is_ok()
                       && value.words.append(std::uint16_t{0x0C0D}).is_ok()
                       && value.name.append(TextView{"a\"b"}).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    return value;
}

[[nodiscard]] DecodeFault list_fault_of(ListImage const& image) noexcept {
    return decode<WithLists>(image.view())
        .match([](WithLists) noexcept { return DecodeFault{DecodeRefusal::bool_byte, 9999}; },
               [](DecodeFault fault) noexcept { return fault; });
}

// The image of each list is its count, its elements, and zero bytes in each
// other room.
void test_lists_encode_their_count_and_elements() {
    static_assert(encoded_size_v<WithLists> == 14);
    ListImage const image = encoded(sample_with_lists());
    constexpr FixedArray<std::uint8_t, 14> expected{0x07, 0x02, 0x0B, 0x0A, 0x0D, 0x0C, 0x00,
                                                    0x00, 0x03, 'a',  '"',  'b',  0x00, 0x00};
    std::size_t at = 0;
    for (std::byte const byte : image) {
        CRUCIBLE_FATAL_INVARIANT(static_cast<std::uint8_t>(byte)
                                 == expected.at(Index<14>::check(at).expect("14 bytes")));
        ++at;
    }
    for (std::byte const byte : encoded(WithLists{})) {
        CRUCIBLE_FATAL_INVARIANT(byte == std::byte{0});
    }
}

// decode gives back random lists, the hash of each is the hand fold of its
// image, and the bytes of a text past its count take no part.
void test_lists_decode_back_and_hash_their_image() {
    ::crucible::test::PhiloxStream stream{0x4C49535453524F55ULL};
    for (int round = 0; round < 4096; ++round) {
        std::uint64_t const word = stream.next_word();
        WithLists value{};
        value.head = static_cast<std::uint8_t>(word);
        std::uint64_t const word_count = (word >> 8) % 4;
        std::uint64_t const name_count = (word >> 16) % 6;
        std::uint64_t const letters = stream.next_word();
        bool is_built = true;
        for (std::uint64_t index = 0; index < word_count; ++index) {
            is_built = is_built && value.words.append(static_cast<std::uint16_t>(letters >> (16 * index))).is_ok();
        }
        for (std::uint64_t index = 0; index < name_count; ++index) {
            is_built = is_built && value.name.append(static_cast<char>(letters >> (8 * index))).is_ok();
        }
        CRUCIBLE_FATAL_INVARIANT(is_built);
        CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
        CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_hash_of_image(encoded(value)));
    }

    WithLists reused = sample_with_lists();
    reused.name.clear();
    WithLists fresh = sample_with_lists();
    fresh.name = Name{};
    bool const is_built = reused.name.append('x').is_ok() && fresh.name.append('x').is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    CRUCIBLE_FATAL_INVARIANT(encoded(reused) == encoded(fresh) && hash(reused) == hash(fresh));
    CRUCIBLE_FATAL_INVARIANT(equal(reused, fresh));
}

// decode refuses a count above the capacity at the offset of the count, and
// a byte of a room past the count at the offset of the first such room.
void test_lists_refuse_each_byte_that_names_no_value() {
    ListImage const clean = encoded(sample_with_lists());

    ListImage too_many_words = clean;
    too_many_words.set(index<1>(), std::byte{4});
    CRUCIBLE_FATAL_INVARIANT((list_fault_of(too_many_words) == DecodeFault{DecodeRefusal::list_count, 1}));

    ListImage word_tail = clean;
    word_tail.set(index<7>(), std::byte{1});
    CRUCIBLE_FATAL_INVARIANT((list_fault_of(word_tail) == DecodeFault{DecodeRefusal::tail_byte, 6}));

    ListImage too_long_name = clean;
    too_long_name.set(index<8>(), std::byte{6});
    CRUCIBLE_FATAL_INVARIANT((list_fault_of(too_long_name) == DecodeFault{DecodeRefusal::list_count, 8}));

    ListImage name_tail = clean;
    name_tail.set(index<13>(), std::byte{1});
    CRUCIBLE_FATAL_INVARIANT((list_fault_of(name_tail) == DecodeFault{DecodeRefusal::tail_byte, 12}));

    // A count at the capacity takes each room, so no byte is a tail.
    ListImage full_name = clean;
    full_name.set(index<8>(), std::byte{5});
    full_name.set(index<12>(), std::byte{'c'});
    full_name.set(index<13>(), std::byte{'d'});
    CRUCIBLE_FATAL_INVARIANT(decode<WithLists>(full_name.view()).is_ok());
}

// The texts at 4 to 71 are a long array of lists at an offset that is a
// multiple of the image of one list.  The words at 73 are a list image of
// 21 bytes at an odd offset.  The ratios at 94 are a list of floats.
struct[[= derive_protocol{}]] WithListArrays {
    std::uint32_t head = 0;
    FixedArray<FixedText<3>, 17> names{};
    std::uint8_t odd = 0;
    ::foundation::core::FixedList<std::uint32_t, 5> words{};
    ::foundation::core::FixedList<float, 2> ratios{};
};

// The hash of each list is the hand fold of its image, also as an element of
// a long array and at an odd offset.  equal of two lists of floats agrees
// with the equality of their images, so +0 and -0 differ.
void test_lists_hash_as_array_elements_at_each_offset() {
    static_assert(encoded_size_v<WithListArrays> == 4 + 17 * 4 + 1 + 21 + 9);
    FixedArray<float, 4> const ratios{0.0f, -0.0f, bit_cast<float>(std::uint32_t{0x7FC00001U}), 1.5f};
    ::crucible::test::PhiloxStream stream{0x4C49535441525241ULL};
    WithListArrays last{};
    for (int round = 0; round < 1024; ++round) {
        std::uint64_t const word = stream.next_word();
        std::uint64_t const letters = stream.next_word();
        WithListArrays value{};
        value.head = static_cast<std::uint32_t>(word);
        value.odd = static_cast<std::uint8_t>(word >> 32);
        bool is_built = true;
        for (auto const index : decltype(value.names)::indices()) {
            std::uint64_t const count = (letters >> (2 * index.raw())) % 4;
            for (std::uint64_t letter = 0; letter < count; ++letter) {
                char const character = static_cast<char>('a' + letter + index.raw());
                is_built = is_built && value.names.update(index, [character](FixedText<3>& name) noexcept {
                    return name.append(character).is_ok();
                });
            }
        }
        for (std::uint64_t index = 0; index < (word >> 40) % 6; ++index) {
            is_built = is_built && value.words.append(static_cast<std::uint32_t>(letters >> index)).is_ok();
        }
        for (std::uint64_t index = 0; index < (word >> 48) % 3; ++index) {
            float const ratio = ratios.at(Index<4>::check((word >> (52 + 2 * index)) % 4).expect("4 ratios"));
            is_built = is_built && value.ratios.append(ratio).is_ok();
        }
        CRUCIBLE_FATAL_INVARIANT(is_built);
        CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
        CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_hash_of_image(encoded(value)));
        bool const has_equal_images = encoded(value) == encoded(last);
        CRUCIBLE_FATAL_INVARIANT(equal(value, last) == has_equal_images);
        CRUCIBLE_FATAL_INVARIANT((hash(value) == hash(last)) == has_equal_images);
        last = value;
    }

    WithListArrays positive{};
    WithListArrays negative{};
    bool const is_built = positive.ratios.append(0.0f).is_ok() && negative.ratios.append(-0.0f).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    CRUCIBLE_FATAL_INVARIANT(!equal(positive, negative) && encoded(positive) != encoded(negative));
    CRUCIBLE_FATAL_INVARIANT(hash(positive) != hash(negative));
}

void test_lists_format_their_elements() {
    FixedText<128> out{};
    TextView const expected{"{head = 7, words = [2571, 3085], name = \"a\\\"b\"}"};
    bool const is_expected = format(out, sample_with_lists())
                                 .match([&expected](TextView text) noexcept { return text == expected; },
                                        [](::foundation::core::Full<char>) noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_expected);
}

// ── An oracle of the sums and the bounded lists ──────────────────────────

using OracleEither = ::foundation::core::OneOf<std::uint8_t, std::uint64_t, bool>;

// The image: head at 0 and 1, the Option of a word at 2 (its payload at 3 to
// 6), the Result at 7 (its room at 8 and 9), the OneOf at 10 (its room at 11
// to 18), the Option of an enumeration at 19 (its room at 20), the list of
// words at 21 (its rooms at 22 to 25), and the text at 26 (its rooms at 27 to
// 29).
struct[[= derive_protocol{}]] OracleValue {
    std::uint16_t head = 0;
    MaybeWord maybe{};
    Outcome outcome{std::uint16_t{0}};
    OracleEither either{std::uint8_t{0}};
    ::foundation::core::Option<SumFault> fault{};
    ::foundation::core::FixedList<std::uint16_t, 2> words{};
    FixedText<3> name{};
};

constexpr std::size_t kOracleBytes = 30;
using OracleImage = FixedArray<std::byte, kOracleBytes>;

[[nodiscard]] std::uint8_t oracle_byte(OracleImage const& image, std::size_t offset) noexcept {
    return static_cast<std::uint8_t>(
        image.at(Index<kOracleBytes>::check(offset).expect("the offset is inside the image")));
}

[[nodiscard]] bool are_oracle_zero(OracleImage const& image, std::size_t first, std::size_t end) noexcept {
    for (std::size_t offset = first; offset < end; ++offset) {
        if (oracle_byte(image, offset) != 0) return false;
    }
    return true;
}

// The first fault of an image in the order of the parts, read byte by byte
// by the rules of the sums and the bounded lists with no call of decode, or
// a fault at 9999 when the image holds a value.
[[nodiscard]] DecodeFault first_oracle_fault(OracleImage const& image) noexcept {
    auto const byte = [&image](std::size_t offset) noexcept { return oracle_byte(image, offset); };
    auto const is_fault_byte = [](std::uint8_t raw) noexcept { return raw == 1 || raw == 2; };
    auto const at = [](DecodeRefusal refusal, std::size_t offset) noexcept {
        return DecodeFault{refusal, static_cast<std::uint32_t>(offset)};
    };

    if (byte(2) > 1) return at(DecodeRefusal::sum_tag, 2);
    if (byte(2) == 0 && !are_oracle_zero(image, 3, 7)) return at(DecodeRefusal::tail_byte, 3);

    if (byte(7) > 1) return at(DecodeRefusal::sum_tag, 7);
    if (byte(7) == 1 && !is_fault_byte(byte(8))) return at(DecodeRefusal::enumerator, 8);
    if (byte(7) == 1 && byte(9) != 0) return at(DecodeRefusal::tail_byte, 9);

    if (byte(10) > 2) return at(DecodeRefusal::sum_tag, 10);
    if (byte(10) == 2 && byte(11) > 1) return at(DecodeRefusal::bool_byte, 11);
    if (byte(10) != 1 && !are_oracle_zero(image, 12, 19)) return at(DecodeRefusal::tail_byte, 12);

    if (byte(19) > 1) return at(DecodeRefusal::sum_tag, 19);
    if (byte(19) == 0 && byte(20) != 0) return at(DecodeRefusal::tail_byte, 20);
    if (byte(19) == 1 && !is_fault_byte(byte(20))) return at(DecodeRefusal::enumerator, 20);

    if (byte(21) > 2) return at(DecodeRefusal::list_count, 21);
    std::size_t const word_end = 22 + 2 * std::size_t{byte(21)};
    if (!are_oracle_zero(image, word_end, 26)) return at(DecodeRefusal::tail_byte, word_end);

    if (byte(26) > 3) return at(DecodeRefusal::list_count, 26);
    std::size_t const name_end = 27 + std::size_t{byte(26)};
    if (!are_oracle_zero(image, name_end, kOracleBytes)) return at(DecodeRefusal::tail_byte, name_end);
    return DecodeFault{DecodeRefusal::bool_byte, 9999};
}

// decode agrees with the oracle on images that random writes change from
// two seeds: each fault is the fault of the oracle, and an accepted image is
// the one image of its value, so encode writes the same bytes back.  Half of
// the writes put a small byte, which names a tag or a count more often.
void test_decode_of_sums_and_lists_agrees_with_an_oracle() {
    static_assert(encoded_size_v<OracleValue> == kOracleBytes);
    OracleValue full{};
    full.head = 0x0102;
    full.maybe = MaybeWord::some(std::uint32_t{0x0A0B0C0D});
    full.outcome = Outcome{::foundation::core::Err<SumFault>{SumFault::second}};
    full.either = OracleEither{bool{true}};
    full.fault = ::foundation::core::Option<SumFault>::some(SumFault::first);
    bool const is_built = full.words.append(std::uint16_t{0x0102}).is_ok() && full.name.append('q').is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    FixedArray<OracleImage, 2> const seeds{encoded(OracleValue{}), encoded(full)};

    ::crucible::test::PhiloxStream stream{0x4F5241434C455331ULL};
    std::size_t accepted = 0;
    for (int round = 0; round < 20000; ++round) {
        std::uint64_t const word = stream.next_word();
        OracleImage image = seeds.at(Index<2>::check(word & 1U).expect("two seeds"));
        for (std::uint64_t write = 0; write <= ((word >> 1) & 3U); ++write) {
            std::uint64_t const change = stream.next_word();
            std::uint64_t const raw = (change & 1U) != 0 ? (change >> 8) % 4 : (change >> 8) & 0xFFU;
            std::byte const byte = static_cast<std::byte>(raw);
            image.set(Index<kOracleBytes>::check((change >> 16) % kOracleBytes).expect("an offset of the image"), byte);
        }
        OracleImage const& read = image;
        DecodeFault const expected = first_oracle_fault(read);
        bool const agrees = decode<OracleValue>(read.view())
                                .match(
                                    [&read, &expected, &accepted](OracleValue value) noexcept {
                                        ++accepted;
                                        return expected.offset() == 9999 && encoded(value) == read;
                                    },
                                    [&expected](DecodeFault fault) noexcept { return fault == expected; });
        CRUCIBLE_FATAL_INVARIANT(agrees);
    }
    CRUCIBLE_FATAL_INVARIANT(accepted > 100 && accepted < 19900);
}

// ── An open enumeration part ──────────────────────────────────────────

// An open enumeration: the two enumerators name two values, and each other
// value of the underlying type is a value too.
enum class[[= ::foundation::core::open_values{}]] Code : std::uint16_t {
    ready = 1,
    busy = 2,
};

// An open part beside a closed part.
struct[[= derive_protocol{}]] HoldsCode {
    Code code = Code::ready;
    Phase phase = Phase::forward;
};

// True when format of value gives the text expected.
[[nodiscard]] bool formats_as(HoldsCode const& value, TextView expected) noexcept {
    FixedText<64> out{};
    return format(out, value)
        .match([&expected](TextView text) noexcept { return text == expected; },
               [](::foundation::core::Full<char>) noexcept { return false; });
}

// decode gives back each value of the open part, also a value that no
// enumerator names.  It still refuses a value of the closed part that names
// no enumerator.  format writes the name of a named value and the number of
// each other value.
void test_open_enumeration_part() {
    static_assert(encoded_size_v<HoldsCode> == 3);
    for (unsigned raw = 0; raw <= 65535; ++raw) {
        HoldsCode const value{.code = enum_from<Code>(static_cast<std::uint16_t>(raw)), .phase = Phase::other};
        CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
    }
    FixedArray<std::byte, encoded_size_v<HoldsCode>> bad_phase = encoded(HoldsCode{});
    bad_phase.set(index<2>(), std::byte{3});
    bool const is_refused =
        decode<HoldsCode>(bad_phase.view())
            .match([](HoldsCode) noexcept { return false; },
                   [](DecodeFault fault) noexcept { return fault == DecodeFault{DecodeRefusal::enumerator, 2}; });
    CRUCIBLE_FATAL_INVARIANT(is_refused);
    CRUCIBLE_FATAL_INVARIANT(
        formats_as(HoldsCode{.code = Code::busy, .phase = Phase::other}, TextView{"{code = busy, phase = other}"}));
    CRUCIBLE_FATAL_INVARIANT(formats_as(HoldsCode{.code = enum_from<Code>(4000), .phase = Phase::forward},
                                        TextView{"{code = 4000, phase = forward}"}));
}

}  // namespace

int main() {
    test_equal_and_order_agree_with_the_hand_forms();
    test_hash_agrees_with_the_hand_fold();
    test_hash_of_each_array_path();
    test_hash_of_a_leaf();
    test_hash_is_foldhash_on_whole_blocks();
    test_encode_writes_the_hand_image();
    test_decode_inverts_encode();
    test_hash_of_a_run();
    test_decode_refuses_each_byte_that_names_no_value();
    test_format_names_each_part();
    test_float_rule();
    test_c_array_and_pointer_parts();
    test_sums_encode_their_tag_and_alternative();
    test_sums_decode_back_and_hash_their_image();
    test_sums_round_trip_random_values();
    test_sums_obey_the_float_rule();
    test_sums_refuse_each_byte_that_names_no_value();
    test_sums_format_their_alternative();
    test_lists_encode_their_count_and_elements();
    test_lists_decode_back_and_hash_their_image();
    test_lists_refuse_each_byte_that_names_no_value();
    test_lists_hash_as_array_elements_at_each_offset();
    test_lists_format_their_elements();
    test_decode_of_sums_and_lists_agrees_with_an_oracle();
    test_open_enumeration_part();
    return 0;
}
