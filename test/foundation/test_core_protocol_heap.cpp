// The heap parts of the Protocol family (THE HEAP PARTS of
// foundation/core/Protocol.h): the image of a class with a HeapArray and a
// HeapText part, its hash, its decode with each refusal, and its format.

#include <foundation/core/Protocol.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/GuardedRegion.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::decode;
using ::foundation::core::DecodeFault;
using ::foundation::core::DecodeRefusal;
using ::foundation::core::derive_protocol;
using ::foundation::core::encode;
using ::foundation::core::Encodable;
using ::foundation::core::encoded_size;
using ::foundation::core::equal;
using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
using ::foundation::core::FixedText;
using ::foundation::core::format;
using ::foundation::core::freeze;
using ::foundation::core::hash;
using ::foundation::core::HeapArray;
using ::foundation::core::HeapDecodable;
using ::foundation::core::HeapEncodable;
using ::foundation::core::HeapText;
using ::foundation::core::mint_grow_text;
using ::foundation::core::mint_heap_array_with;
using ::foundation::core::mint_heap_array_zeroed;
using ::foundation::core::TextView;
using ::foundation::core::View;

enum class Kind : std::uint8_t {
    first = 1,
    second = 2,
};

struct[[= derive_protocol{}]] Point {
    std::uint16_t x = 0;
    bool flag = false;
};

// The image: id at 0 to 3, the count of values at 4 and two bytes for each
// value, the count of the name and one byte for each character, the count
// of points and three bytes for each point, and then kind.
struct[[= derive_protocol{}]] Message {
    std::uint32_t id = 0;
    HeapArray<std::uint16_t> values{};
    HeapText name = freeze(mint_grow_text(0));
    HeapArray<Point> points{};
    Kind kind = Kind::first;
};

static_assert(HeapEncodable<Message> && HeapDecodable<Message> && !Encodable<Message>);
static_assert(Encodable<Point> && !HeapEncodable<Point>);

using Bytes = HeapArray<std::byte>;
using HandImage = FixedList<std::byte, 128>;
using Budget = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

// A budget far above the storage of each value of the tests that do not test
// the budget.
constexpr Budget kWideBudget{std::size_t{1} << 20};

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

[[nodiscard]] HeapText text_of(TextView characters) noexcept {
    ::foundation::core::GrowText grown = mint_grow_text(characters.size());
    bool const is_put = grown.append(test_alloc(), characters).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_put);
    return freeze(std::move(grown));
}

// Writes byte at an offset of an image.
void put_byte(Bytes& image, std::size_t offset, std::byte byte) noexcept {
    image.with_element(
        offset, [byte](std::byte& held) noexcept { held = byte; },
        [] noexcept { ::foundation::core::fatal("the offset is inside the image"); });
}

// The image of a value, by encode, in storage of exactly its size.
template <HeapEncodable T>
[[nodiscard]] Bytes image_of(T const& value) noexcept {
    Bytes image = mint_heap_array_zeroed<std::byte>(test_alloc(), encoded_size(value));
    image =
        ::foundation::core::with_mut(std::move(image), [&value](auto bytes) noexcept { encode(value, bytes.view()); });
    return image;
}

[[nodiscard]] Message sample_message() noexcept {
    Message value{};
    value.id = 0x01020304U;
    value.values = mint_heap_array_with<std::uint16_t>(test_alloc(), 2, [](std::size_t position) noexcept {
        return position == 0 ? std::uint16_t{0x0A0B} : std::uint16_t{0x0C0D};
    });
    value.name = text_of(TextView{"a\"b"});
    value.points =
        mint_heap_array_with<Point>(test_alloc(), 1, [](std::size_t) noexcept { return Point{0x0102, true}; });
    value.kind = Kind::second;
    return value;
}

// Puts the bytes of word after the last byte of the hand image, the low byte
// first.
template <class Word>
void put_word(HandImage& image, Word word) noexcept {
    for (std::size_t index = 0; index < sizeof(Word); ++index) {
        bool const is_put = image
                                .append(static_cast<std::byte>(
                                    static_cast<std::uint8_t>(static_cast<std::uint64_t>(word) >> (8 * index))))
                                .is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_put);
    }
}

[[nodiscard]] bool has_bytes(Bytes const& image, HandImage const& hand) noexcept {
    return ::foundation::core::equal(image.view(), hand.view());
}

// The hand fold of the hash of an image of run-time size: blocks of 16
// bytes, the last one filled with zero bytes, and then one block with the
// count of the bytes in its low word.
constexpr std::uint64_t kFoldSeed = 0x243f6a8885a308d3ULL;
constexpr std::uint64_t kStartSeed = 0x13198a2e03707344ULL;

[[nodiscard]] std::uint64_t folded_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    ::foundation::core::WideProduct<std::uint64_t> const product = ::foundation::core::widening_mul(left, right);
    return product.low ^ product.high;
}

[[nodiscard]] std::uint64_t hand_heap_hash(View<std::byte const> image) noexcept {
    std::uint64_t accumulator = kStartSeed;
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    std::size_t lane = 0;
    for (std::byte const byte : image) {
        std::uint64_t const word = static_cast<std::uint8_t>(byte);
        if (lane < 8) {
            low |= word << (8 * lane);
        } else {
            high |= word << (8 * (lane - 8));
        }
        ++lane;
        if (lane == 16) {
            accumulator = folded_multiply(low ^ accumulator, high ^ kFoldSeed);
            low = 0;
            high = 0;
            lane = 0;
        }
    }
    if (lane != 0) accumulator = folded_multiply(low ^ accumulator, high ^ kFoldSeed);
    std::uint64_t const length = image.size();
    return folded_multiply(length ^ accumulator, kFoldSeed);
}

// The fault of decode of the bytes with the budget, or a fault at 9999 when
// the bytes hold a value.
template <HeapDecodable T>
[[nodiscard]] DecodeFault fault_with(Budget budget, View<std::byte const> bytes) noexcept {
    return decode<T>(test_alloc(), budget, bytes)
        .match([](T) noexcept { return DecodeFault{DecodeRefusal::bool_byte, 9999}; },
               [](DecodeFault fault) noexcept { return fault; });
}

[[nodiscard]] DecodeFault fault_of(View<std::byte const> bytes) noexcept {
    return fault_with<Message>(kWideBudget, bytes);
}

// True when decode of the image of value with the budget gives value back.
template <HeapDecodable T>
[[nodiscard]] bool decodes_back_with(Budget budget, T const& value) noexcept {
    Bytes const image = image_of(value);
    return decode<T>(test_alloc(), budget, image.view())
        .match([&value](T back) noexcept { return equal(back, value); }, [](DecodeFault) noexcept { return false; });
}

[[nodiscard]] bool decodes_back(Message const& value) noexcept { return decodes_back_with(kWideBudget, value); }

// The image is each part in order: a part of fixed size takes THE IMAGE, and
// a heap part takes its count in 8 bytes and then its elements.
void test_heap_image_is_the_hand_image() {
    Message const value = sample_message();
    HandImage hand{};
    put_word(hand, std::uint32_t{0x01020304U});
    put_word(hand, std::uint64_t{2});
    put_word(hand, std::uint16_t{0x0A0B});
    put_word(hand, std::uint16_t{0x0C0D});
    put_word(hand, std::uint64_t{3});
    for (char const character : TextView{"a\"b"})
        put_word(hand, static_cast<std::uint8_t>(character));
    put_word(hand, std::uint64_t{1});
    put_word(hand, std::uint16_t{0x0102});
    put_word(hand, std::uint8_t{1});
    put_word(hand, std::uint8_t{2});
    CRUCIBLE_FATAL_INVARIANT(encoded_size(value) == 39);
    CRUCIBLE_FATAL_INVARIANT(has_bytes(image_of(value), hand));

    HandImage empty{};
    put_word(empty, std::uint32_t{0});
    put_word(empty, std::uint64_t{0});
    put_word(empty, std::uint64_t{0});
    put_word(empty, std::uint64_t{0});
    put_word(empty, std::uint8_t{1});
    CRUCIBLE_FATAL_INVARIANT(encoded_size(Message{}) == 29);
    CRUCIBLE_FATAL_INVARIANT(has_bytes(image_of(Message{}), empty));
}

// decode gives back random values, and the hash of each is the hand fold of
// its image and its length.
void test_heap_decode_inverts_encode() {
    ::crucible::test::PhiloxStream stream{0x4845415050524F54ULL};
    for (int round = 0; round < 2048; ++round) {
        std::uint64_t const word = stream.next_word();
        std::uint64_t const letters = stream.next_word();
        Message value{};
        value.id = static_cast<std::uint32_t>(word);
        value.values = mint_heap_array_with<std::uint16_t>(
            test_alloc(), (word >> 32) % 20, [&letters](std::size_t position) noexcept {
                return static_cast<std::uint16_t>(letters >> (position % 4 * 16));
            });
        FixedText<24> characters{};
        bool is_built = true;
        for (std::uint64_t index = 0; index < (word >> 40) % 24; ++index) {
            is_built = is_built && characters.append(static_cast<char>(letters >> (index % 8 * 8))).is_ok();
        }
        CRUCIBLE_FATAL_INVARIANT(is_built);
        value.name = text_of(characters.view());
        value.points =
            mint_heap_array_with<Point>(test_alloc(), (word >> 48) % 5, [&letters](std::size_t position) noexcept {
                return Point{static_cast<std::uint16_t>(letters >> position), ((letters >> position) & 1U) != 0};
            });
        value.kind = ((word >> 56) & 1U) != 0 ? Kind::first : Kind::second;
        CRUCIBLE_FATAL_INVARIANT(decodes_back(value));
        Bytes const image = image_of(value);
        CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_heap_hash(image.view()));
    }
    CRUCIBLE_FATAL_INVARIANT(decodes_back(Message{}));
    CRUCIBLE_FATAL_INVARIANT(hash(Message{}) != hash(sample_message()));
}

// decode refuses bytes that end inside a part at the offset of the part,
// a count that needs more bytes than remain before it allocates, a byte
// after the image, and each fault of a part of fixed size.  A fault inside
// an element of an array that decode allocated gives the storage back,
// which the leak check of the sanitizer build shows.
void test_heap_decode_refuses_each_fault() {
    Bytes const image = image_of(sample_message());
    View<std::byte const> const whole = image.view();

    for (std::size_t length = 0; length < whole.size(); ++length) {
        DecodeFault const fault = fault_of(whole.window(0, length).expect("a prefix of the image"));
        CRUCIBLE_FATAL_INVARIANT(fault.refusal() == DecodeRefusal::image_end && fault.offset() <= length);
    }
    CRUCIBLE_FATAL_INVARIANT(
        (fault_of(whole.window(0, 3).expect("a prefix")) == DecodeFault{DecodeRefusal::image_end, 0}));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_of(whole.window(0, 14).expect("a prefix")) == DecodeFault{DecodeRefusal::image_end, 4}));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_of(whole.window(0, 38).expect("a prefix")) == DecodeFault{DecodeRefusal::image_end, 38}));

    Bytes longer = mint_heap_array_zeroed<std::byte>(test_alloc(), whole.size() + 1);
    longer = ::foundation::core::with_mut(std::move(longer), [&whole](auto bytes) noexcept {
        encode(sample_message(), bytes.view().window(0, whole.size()).expect("the image fits"));
    });
    Bytes const& longer_read = longer;
    CRUCIBLE_FATAL_INVARIANT((fault_of(longer_read.view()) == DecodeFault{DecodeRefusal::trailing_byte, 39}));

    Bytes huge_count = image_of(sample_message());
    put_byte(huge_count, 9, std::byte{0x01});
    Bytes const& huge_read = huge_count;
    CRUCIBLE_FATAL_INVARIANT((fault_of(huge_read.view()) == DecodeFault{DecodeRefusal::image_end, 4}));

    Bytes bad_kind = image_of(sample_message());
    put_byte(bad_kind, 38, std::byte{3});
    Bytes const& kind_read = bad_kind;
    CRUCIBLE_FATAL_INVARIANT((fault_of(kind_read.view()) == DecodeFault{DecodeRefusal::enumerator, 38}));

    Bytes bad_flag = image_of(sample_message());
    put_byte(bad_flag, 37, std::byte{2});
    Bytes const& flag_read = bad_flag;
    CRUCIBLE_FATAL_INVARIANT((fault_of(flag_read.view()) == DecodeFault{DecodeRefusal::bool_byte, 37}));
}

// An element of 4 bytes that is a list and no leaf: its count and three
// rooms.
using Tag = FixedList<std::uint8_t, 3>;

struct[[= derive_protocol{}]] Tagged {
    std::uint8_t head = 0;
    HeapArray<Tag> tags{};
};

// The hash of a HeapArray of lists folds the image of each list, and no raw
// byte of the list, so the hash is the hand fold of the image.
void test_heap_hash_of_an_array_of_lists() {
    ::crucible::test::PhiloxStream stream{0x4845415054414753ULL};
    for (int round = 0; round < 512; ++round) {
        std::uint64_t const word = stream.next_word();
        Tagged value{};
        value.head = static_cast<std::uint8_t>(word);
        value.tags = mint_heap_array_with<Tag>(test_alloc(), (word >> 8) % 9, [word](std::size_t position) noexcept {
            Tag tag{};
            std::uint64_t const count = (word >> (16 + 2 * (position % 8))) % 4;
            bool is_built = true;
            for (std::uint64_t index = 0; index < count; ++index) {
                is_built = is_built && tag.append(static_cast<std::uint8_t>(position * 7 + index)).is_ok();
            }
            if (!is_built) ::foundation::core::unreachable();
            return tag;
        });
        Bytes image = mint_heap_array_zeroed<std::byte>(test_alloc(), encoded_size(value));
        image = ::foundation::core::with_mut(std::move(image),
                                             [&value](auto bytes) noexcept { encode(value, bytes.view()); });
        Bytes const& read = image;
        CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_heap_hash(read.view()));
        bool const is_back = decode<Tagged>(test_alloc(), kWideBudget, read.view())
                                 .match([&value](Tagged back) noexcept { return equal(back, value); },
                                        [](DecodeFault) noexcept { return false; });
        CRUCIBLE_FATAL_INVARIANT(is_back);
    }
}

// Runs of leaves at each kind of position: the words of AlignedWords start
// at 12, a multiple of their 4 bytes, and the words of OddWords start at 9,
// which is no multiple.  The wide words of AlignedWide start at 8, and those
// of OddWide at 9.
struct[[= derive_protocol{}]] AlignedWords {
    std::uint32_t head = 0;
    HeapArray<std::uint32_t> words{};
};

struct[[= derive_protocol{}]] OddWords {
    std::uint8_t head = 0;
    HeapArray<std::uint32_t> words{};
};

struct[[= derive_protocol{}]] AlignedWide {
    HeapArray<std::uint64_t> words{};
};

struct[[= derive_protocol{}]] OddWide {
    std::uint8_t head = 0;
    HeapArray<std::uint64_t> words{};
};

// The hash of value is the hand fold of its image.
template <class T>
[[nodiscard]] bool hashes_as_its_image(T const& value) noexcept {
    Bytes image = mint_heap_array_zeroed<std::byte>(test_alloc(), encoded_size(value));
    image =
        ::foundation::core::with_mut(std::move(image), [&value](auto bytes) noexcept { encode(value, bytes.view()); });
    Bytes const& read = image;
    return hash(value).raw() == hand_heap_hash(read.view());
}

// The fold of a run of leaves takes whole blocks only from the start of a
// block, so each count of words at each kind of position gives the hand fold.
void test_heap_hash_of_runs_at_each_position() {
    ::crucible::test::PhiloxStream stream{0x52554E5341544F46ULL};
    for (std::size_t count = 0; count < 41; ++count) {
        std::uint64_t const seed = stream.next_word();
        auto const word_at = [seed](std::size_t position) noexcept {
            return static_cast<std::uint32_t>(seed >> (position % 32)) ^ static_cast<std::uint32_t>(position);
        };
        auto const wide_at = [seed](std::size_t position) noexcept -> std::uint64_t {
            return seed ^ (position * std::uint64_t{0x9E3779B97F4A7C15ULL});
        };
        AlignedWords aligned{};
        aligned.head = static_cast<std::uint32_t>(seed);
        aligned.words = mint_heap_array_with<std::uint32_t>(test_alloc(), count, word_at);
        OddWords odd{};
        odd.head = static_cast<std::uint8_t>(seed);
        odd.words = mint_heap_array_with<std::uint32_t>(test_alloc(), count, word_at);
        AlignedWide aligned_wide{};
        aligned_wide.words = mint_heap_array_with<std::uint64_t>(test_alloc(), count, wide_at);
        OddWide odd_wide{};
        odd_wide.head = static_cast<std::uint8_t>(seed);
        odd_wide.words = mint_heap_array_with<std::uint64_t>(test_alloc(), count, wide_at);
        CRUCIBLE_FATAL_INVARIANT(hashes_as_its_image(aligned) && hashes_as_its_image(odd));
        CRUCIBLE_FATAL_INVARIANT(hashes_as_its_image(aligned_wide) && hashes_as_its_image(odd_wide));
    }
}

void test_heap_format_names_each_part() {
    FixedText<160> out{};
    TextView const expected{
        "{id = 16909060, values = [2571, 3085], name = \"a\\\"b\", points = [{x = 258, flag = true}], "
        "kind = second}"};
    bool const is_expected = format(out, sample_message())
                                 .match([&expected](TextView text) noexcept { return text == expected; },
                                        [](::foundation::core::Full<char>) noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_expected);
}

// ── An oracle of the heap parts ──────────────────────────────────────────

[[nodiscard]] std::uint8_t byte_of(View<std::byte const> bytes, std::size_t at) noexcept {
    return static_cast<std::uint8_t>(*bytes.get(at).expect("the offset is inside the bytes"));
}

// The little-endian count of 8 bytes from at.  The caller makes sure that
// the bytes lie in the input.
[[nodiscard]] std::uint64_t count_of(View<std::byte const> bytes, std::size_t at) noexcept {
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        count |= static_cast<std::uint64_t>(byte_of(bytes, at + index)) << (8 * index);
    }
    return count;
}

// One heap part of a Message for the oracle: the bytes of the image of one
// element, and the bytes of its storage.  Each storage is a multiple of the
// alignment of its element, so the storage of a count is the product.
struct HeapPartShape final {
    std::size_t step = 0;
    std::size_t storage = 0;
};

// The first fault of the bytes of a Message in the order of the parts, read
// by the rules of THE HEAP PARTS with no call of decode, or a fault at 9999
// when the bytes hold a value.  A part that the bytes cut names the offset
// of its first byte.  A heap part whose storage needs more than the budget
// that remains names the offset of its count.
[[nodiscard]] DecodeFault first_heap_fault(Budget budget, View<std::byte const> bytes) noexcept {
    std::size_t const size = bytes.size();
    auto const fault = [](DecodeRefusal refusal, std::size_t offset) noexcept {
        return DecodeFault{refusal, static_cast<std::uint32_t>(offset)};
    };
    std::size_t at = 0;
    std::size_t left = budget.raw();
    if (size - at < 4) return fault(DecodeRefusal::image_end, at);
    at += 4;

    FixedArray<HeapPartShape, 3> const shapes{HeapPartShape{2, sizeof(std::uint16_t)}, HeapPartShape{1, 1},
                                              HeapPartShape{3, sizeof(Point)}};
    for (HeapPartShape const shape : shapes) {
        std::size_t const step = shape.step;
        if (size - at < 8) return fault(DecodeRefusal::image_end, at);
        std::uint64_t const count = count_of(bytes, at);
        if (count > (size - at - 8) / step) return fault(DecodeRefusal::image_end, at);
        if (count * shape.storage > left) return fault(DecodeRefusal::allocation_budget, at);
        left -= count * shape.storage;
        at += 8;
        for (std::uint64_t index = 0; index < count; ++index) {
            if (step == 3 && byte_of(bytes, at + 2) > 1) return fault(DecodeRefusal::bool_byte, at + 2);
            at += step;
        }
    }

    if (size - at < 1) return fault(DecodeRefusal::image_end, at);
    std::uint8_t const kind = byte_of(bytes, at);
    if (kind != 1 && kind != 2) return fault(DecodeRefusal::enumerator, at);
    at += 1;
    if (at != size) return fault(DecodeRefusal::trailing_byte, at);
    return DecodeFault{DecodeRefusal::bool_byte, 9999};
}

// decode agrees with the oracle on bytes that random cuts, growths and
// writes change from two seeds: each fault is the fault of the oracle, and
// an accepted value is the one value of its bytes, so encode writes the same
// bytes back and the hash is the hand fold of the bytes.  A write that puts
// a large byte into a count makes a count that needs more bytes than remain,
// which decode refuses before it allocates.  One round in four has a budget
// of at most 15 bytes, so decode also refuses storage past the budget.
void test_heap_decode_agrees_with_an_oracle() {
    FixedArray<Bytes, 2> const seeds{image_of(Message{}), image_of(sample_message())};
    ::crucible::test::PhiloxStream stream{0x4845415052414E44ULL};
    std::size_t accepted = 0;
    std::size_t over_budget = 0;
    for (int round = 0; round < 20000; ++round) {
        std::uint64_t const word = stream.next_word();
        Bytes const& seed = seeds.at(::foundation::core::Index<2>::check(word & 1U).expect("two seeds"));
        std::size_t const seed_size = seed.size();
        std::uint64_t const cut = (word >> 1) % 4 == 0 ? (word >> 8) % 17 : 8;
        std::size_t const size = seed_size + 8 >= cut ? seed_size + 8 - cut : 0;
        std::uint64_t const filler = stream.next_word();
        Bytes bytes =
            mint_heap_array_with<std::byte>(test_alloc(), size, [&seed, seed_size, filler](std::size_t at) noexcept {
                if (at < seed_size) return *seed.view().get(at).expect("a byte of the seed");
                return static_cast<std::byte>(static_cast<std::uint8_t>(filler >> (8 * (at % 8))));
            });
        bytes = ::foundation::core::with_mut(std::move(bytes), [&stream, word, size](auto cells) noexcept {
            for (std::uint64_t write = 0; size != 0 && write < ((word >> 6) & 3U); ++write) {
                std::uint64_t const change = stream.next_word();
                std::uint64_t const raw = (change & 1U) != 0 ? (change >> 8) % 4 : (change >> 8) & 0xFFU;
                *cells.get((change >> 16) % size).expect("the offset is inside the image") =
                    static_cast<std::byte>(raw);
            }
        });
        std::uint64_t const purse = stream.next_word();
        Budget const budget = purse % 4 == 0 ? Budget{(purse >> 8) % 16} : kWideBudget;
        Bytes const& read = bytes;
        DecodeFault const expected = first_heap_fault(budget, read.view());
        if (expected.refusal() == DecodeRefusal::allocation_budget) ++over_budget;
        bool const agrees = decode<Message>(test_alloc(), budget, read.view())
                                .match(
                                    [&read, &expected, &accepted](Message value) noexcept {
                                        ++accepted;
                                        if (expected.offset() != 9999 || encoded_size(value) != read.size())
                                            return false;
                                        Bytes const again = image_of(value);
                                        return ::foundation::core::equal(again.view(), read.view())
                                            && hash(value).raw() == hand_heap_hash(read.view());
                                    },
                                    [&expected](DecodeFault fault) noexcept { return fault == expected; });
        CRUCIBLE_FATAL_INVARIANT(agrees);
    }
    CRUCIBLE_FATAL_INVARIANT(accepted > 100 && accepted < 19900);
    CRUCIBLE_FATAL_INVARIANT(over_budget > 100);
}

// ── The allocation budget ────────────────────────────────────────────────

// An element whose image is two bytes and whose storage is 64 bytes, because
// a local part holds a scratch area.
struct[[= derive_protocol{}]] Sampled {
    std::uint16_t value = 0;
    [[= ::foundation::core::local_part{}]] FixedArray<std::uint8_t, 62> scratch{};
};

// The image: head at 0, the count of the samples at 1 and two bytes for each
// sample, and then the count of the label and one byte for each character.
struct[[= derive_protocol{}]] Samples {
    std::uint8_t head = 0;
    HeapArray<Sampled> samples{};
    HeapText label = freeze(mint_grow_text(0));
};

// An element whose image is one byte and whose storage is more than 16 MiB,
// because a local part holds a cache.
struct[[= derive_protocol{}]] Cached {
    std::uint8_t tag = 0;
    [[= ::foundation::core::local_part{}]] FixedArray<std::byte, std::size_t{1} << 24> cache{};
};

struct[[= derive_protocol{}]] CachedList {
    HeapArray<Cached> items{};
};

static_assert(sizeof(Sampled) == 64 && HeapDecodable<Samples> && HeapDecodable<CachedList>);

// Three samples and a label of five characters: an image of 28 bytes whose
// heap parts allocate 3 * 64 + 5 = 197 bytes.
[[nodiscard]] Samples sample_samples() noexcept {
    Samples value{};
    value.head = 7;
    value.samples = mint_heap_array_with<Sampled>(test_alloc(), 3, [](std::size_t position) noexcept {
        Sampled sample{};
        sample.value = static_cast<std::uint16_t>(0x0100U + position);
        return sample;
    });
    value.label = text_of(TextView{"hello"});
    return value;
}

// decode takes the storage of each heap part from the budget before it
// allocates.  A budget of exactly the 197 bytes of the value gives the value
// back.  Each smaller budget refuses the first heap part that does not fit,
// at the offset of its count: the label at 15, or the samples at 1.  A value
// with no element and no character needs no storage, so a budget of zero
// takes it.
void test_heap_decode_refuses_storage_past_the_budget() {
    Samples const value = sample_samples();
    Bytes const image = image_of(value);
    View<std::byte const> const read = image.view();
    CRUCIBLE_FATAL_INVARIANT(read.size() == 28);
    CRUCIBLE_FATAL_INVARIANT(decodes_back_with(Budget{197}, value));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_with<Samples>(Budget{196}, read) == DecodeFault{DecodeRefusal::allocation_budget, 15}));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_with<Samples>(Budget{192}, read) == DecodeFault{DecodeRefusal::allocation_budget, 15}));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_with<Samples>(Budget{191}, read) == DecodeFault{DecodeRefusal::allocation_budget, 1}));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_with<Samples>(Budget{0}, read) == DecodeFault{DecodeRefusal::allocation_budget, 1}));
    CRUCIBLE_FATAL_INVARIANT(decodes_back_with(Budget{0}, Samples{}));
}

// A heap part of a wider alignment takes the storage that its mint
// allocates: three bytes at an alignment of 64 take one line of 64 bytes.
struct[[= derive_protocol{}]] Lined {
    HeapArray<std::uint8_t, 64> line{};
};

void test_heap_decode_charges_the_rounded_storage() {
    Lined value{};
    value.line = mint_heap_array_with<std::uint8_t, 64>(
        test_alloc(), 3, [](std::size_t position) noexcept { return static_cast<std::uint8_t>(position + 1); });
    Bytes const image = image_of(value);
    CRUCIBLE_FATAL_INVARIANT(decodes_back_with(Budget{64}, value));
    CRUCIBLE_FATAL_INVARIANT(
        (fault_with<Lined>(Budget{63}, image.view()) == DecodeFault{DecodeRefusal::allocation_budget, 0}));
}

// A hostile input: a count of 2^23 elements of an image of one byte, which
// the 8 MiB after the count hold, and of 16 MiB of storage each.  The storage
// of the count is 2^47 bytes, which no allocation can give, and decode
// refuses the count before it allocates.  One element takes its 16 MiB and
// one byte when the budget holds them.
void test_heap_decode_refuses_a_hostile_count() {
    Bytes hostile = mint_heap_array_zeroed<std::byte>(test_alloc(), 8 + (std::size_t{1} << 23));
    put_byte(hostile, 2, std::byte{0x80});
    Bytes const& read = hostile;
    CRUCIBLE_FATAL_INVARIANT((fault_with<CachedList>(Budget{std::size_t{1} << 30}, read.view())
                              == DecodeFault{DecodeRefusal::allocation_budget, 0}));

    Bytes single = mint_heap_array_zeroed<std::byte>(test_alloc(), 9);
    put_byte(single, 0, std::byte{1});
    put_byte(single, 8, std::byte{0x5A});
    Bytes const& single_read = single;
    Budget const storage{sizeof(Cached)};
    bool const is_taken =
        decode<CachedList>(test_alloc(), storage, single_read.view())
            .match(
                [](CachedList list) noexcept {
                    return list.items.size() == 1 && (*list.items.view().get(0).expect("one element")).tag == 0x5A;
                },
                [](DecodeFault) noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_taken);
    CRUCIBLE_FATAL_INVARIANT((fault_with<CachedList>(Budget{sizeof(Cached) - 1}, single_read.view())
                              == DecodeFault{DecodeRefusal::allocation_budget, 0}));
}

// An image holds at most 2^32 - 1 bytes, a limit of the format, so decode
// refuses a View of 2^32 bytes before it reads a byte, and names the cap as
// the offset.  A View of 2^32 - 1 zero bytes passes the cap: its first fault
// is the kind of a Message at 28, which holds no enumerator.  The bytes come
// from a map that nothing writes, so the test touches one page of the 4 GiB.
void test_heap_decode_refuses_more_than_the_cap() {
    constexpr std::size_t cap = 0xFFFFFFFFU;
    ::foundation::os::GuardedRegion region =
        ::foundation::os::map_guarded_region(::foundation::core::ByteCount{cap + 1})
            .expect("a map of 4 GiB that no page backs");
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the kernel gives the pages of a map as a pointer to raw bytes and a count")
    View<std::byte const> const mapped = ::foundation::core::mint_foreign_view(
        static_cast<std::byte const*>(static_cast<void const*>(region.data_mut())), cap + 1);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT((fault_of(mapped) == DecodeFault{DecodeRefusal::image_size, 0xFFFFFFFFU}));
    CRUCIBLE_FATAL_INVARIANT((fault_of(mapped.window(0, cap).expect("the cap is inside the map"))
                              == DecodeFault{DecodeRefusal::enumerator, 28}));
}

}  // namespace

int main() {
    test_heap_image_is_the_hand_image();
    test_heap_decode_inverts_encode();
    test_heap_decode_refuses_each_fault();
    test_heap_hash_of_an_array_of_lists();
    test_heap_hash_of_runs_at_each_position();
    test_heap_format_names_each_part();
    test_heap_decode_agrees_with_an_oracle();
    test_heap_decode_refuses_storage_past_the_budget();
    test_heap_decode_charges_the_rounded_storage();
    test_heap_decode_refuses_a_hostile_count();
    test_heap_decode_refuses_more_than_the_cap();
    return 0;
}
