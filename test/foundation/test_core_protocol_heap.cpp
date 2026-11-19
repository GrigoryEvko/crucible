// The heap parts of the Protocol family (THE HEAP PARTS of
// foundation/core/Protocol.h): the image of a class with a HeapArray and a
// HeapText part, its hash, its decode with each refusal, and its format.

#include <foundation/core/Protocol.h>

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

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

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

[[nodiscard]] HeapText text_of(TextView characters) noexcept {
    ::foundation::core::GrowText grown = mint_grow_text(characters.size());
    bool const is_put = grown.append(test_alloc(), characters).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_put);
    return freeze(std::move(grown));
}

// The byte at an offset of an image, to write.
[[nodiscard]] std::byte& byte_at(Bytes& image, std::size_t offset) noexcept {
    return *image.view().get(offset).expect("the offset is inside the image");
}

// The image of a value, by encode, in storage of exactly its size.
[[nodiscard]] Bytes image_of(Message const& value) noexcept {
    Bytes image = mint_heap_array_zeroed<std::byte>(test_alloc(), encoded_size(value));
    encode(value, image.view());
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

[[nodiscard]] DecodeFault fault_of(View<std::byte const> bytes) noexcept {
    return decode<Message>(test_alloc(), bytes)
        .match([](Message) noexcept { return DecodeFault{DecodeRefusal::bool_byte, 9999}; },
               [](DecodeFault fault) noexcept { return fault; });
}

[[nodiscard]] bool decodes_back(Message const& value) noexcept {
    Bytes const image = image_of(value);
    return decode<Message>(test_alloc(), image.view())
        .match([&value](Message back) noexcept { return equal(back, value); },
               [](DecodeFault) noexcept { return false; });
}

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
    encode(sample_message(), longer.view().window(0, whole.size()).expect("the image fits"));
    Bytes const& longer_read = longer;
    CRUCIBLE_FATAL_INVARIANT((fault_of(longer_read.view()) == DecodeFault{DecodeRefusal::trailing_byte, 39}));

    Bytes huge_count = image_of(sample_message());
    byte_at(huge_count, 9) = std::byte{0x01};
    Bytes const& huge_read = huge_count;
    CRUCIBLE_FATAL_INVARIANT((fault_of(huge_read.view()) == DecodeFault{DecodeRefusal::image_end, 4}));

    Bytes bad_kind = image_of(sample_message());
    byte_at(bad_kind, 38) = std::byte{3};
    Bytes const& kind_read = bad_kind;
    CRUCIBLE_FATAL_INVARIANT((fault_of(kind_read.view()) == DecodeFault{DecodeRefusal::enumerator, 38}));

    Bytes bad_flag = image_of(sample_message());
    byte_at(bad_flag, 37) = std::byte{2};
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
        encode(value, image.view());
        Bytes const& read = image;
        CRUCIBLE_FATAL_INVARIANT(hash(value).raw() == hand_heap_hash(read.view()));
        bool const is_back = decode<Tagged>(test_alloc(), read.view())
                                 .match([&value](Tagged back) noexcept { return equal(back, value); },
                                        [](DecodeFault) noexcept { return false; });
        CRUCIBLE_FATAL_INVARIANT(is_back);
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

}  // namespace

int main() {
    test_heap_image_is_the_hand_image();
    test_heap_decode_inverts_encode();
    test_heap_decode_refuses_each_fault();
    test_heap_hash_of_an_array_of_lists();
    test_heap_format_names_each_part();
    return 0;
}
