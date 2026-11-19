// The compile-time checks of foundation/core/Protocol.h.

#include <foundation/core/Protocol.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core {

namespace detail::protocol_checks {

enum class Kind : std::uint8_t {
    first,
    second,
};

struct[[= derive_protocol{}]] Leaves {
    std::uint16_t word = 0;
    Kind kind = Kind::first;
    bool flag = false;
    double scale = 0.0;
};

// A class with no annotation takes no part in the image, also when its own
// operators exist.
struct Unmarked {
    std::uint32_t word = 0;
    friend constexpr bool operator==(Unmarked, Unmarked) noexcept = default;
};

// A local part has no place in the image, so its type needs no image.
// test_core_protocol holds a local part of a pointer.
struct[[= derive_protocol{}]] HoldsLocalPart {
    std::uint32_t word = 0;
    [[= local_part{}]] Unmarked handle{};
};

// A union and a bit-field have no image.  The fixtures of the family hold a
// pointer.
union Either {
    int whole;
    float part;
};

struct[[= derive_protocol{}]] HoldsUnion {
    Either either{};
};

struct[[= derive_protocol{}]] HoldsBitField {
    std::uint32_t flag : 1 = 0;
};

struct[[= derive_protocol{}]] HoldsUnmarked {
    Unmarked unmarked{};
};

// A const member and a class whose values only its constructors make are
// refused by decode, and not by encode.
struct[[= derive_protocol{}]] HoldsConst {
    std::uint32_t const word = 0;
};

class[[= derive_protocol{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Sealed {
public:
    constexpr Sealed() noexcept = default;

private:
    std::uint32_t word_ = 0;
};

static_assert(Encodable<Leaves> && Decodable<Leaves>);
static_assert(encoded_size_v<Leaves> == 2 + 1 + 1 + 8);
static_assert(Encodable<HoldsLocalPart> && Decodable<HoldsLocalPart> && encoded_size_v<HoldsLocalPart> == 4);
static_assert(!Encodable<HoldsUnion> && !Encodable<HoldsBitField>);
static_assert(!Encodable<HoldsUnmarked> && !Encodable<Unmarked> && !Encodable<int>);
static_assert(Encodable<HoldsConst> && !Decodable<HoldsConst>);
static_assert(Encodable<Sealed> && !Decodable<Sealed>);

// A FixedArray part takes the bytes of its elements.  test_core_protocol
// compares it with a C array part.
struct[[= derive_protocol{}]] HoldsFixedArray {
    FixedArray<std::uint32_t, 3> lanes{};
};

static_assert(encoded_size_v<HoldsFixedArray> == 12);

// The image is little-endian and holds no padding byte, also in a constant
// evaluation.
[[nodiscard]] consteval bool writes_the_image() {
    Leaves leaves{};
    leaves.word = 0x0102;
    leaves.kind = Kind::second;
    leaves.flag = true;
    leaves.scale = -0.0;
    FixedArray<std::byte, encoded_size_v<Leaves>> image{};
    encode(leaves, image.view());
    return image.at(index<0>()) == std::byte{0x02} && image.at(index<1>()) == std::byte{0x01}
        && image.at(index<2>()) == std::byte{1} && image.at(index<3>()) == std::byte{1}
        && image.at(index<4>()) == std::byte{0} && image.at(index<11>()) == std::byte{0x80};
}
static_assert(writes_the_image());

// decode inverts encode, and refuses a bool byte past 1 at its offset.
[[nodiscard]] consteval bool reads_the_image() {
    Leaves leaves{};
    leaves.word = 7;
    leaves.kind = Kind::second;
    leaves.scale = 2.5;
    FixedArray<std::byte, encoded_size_v<Leaves>> image{};
    encode(leaves, image.view());
    FixedArray<std::byte, encoded_size_v<Leaves>> const& read = image;
    bool const is_back = decode<Leaves>(read.view())
                             .match([&leaves](Leaves back) noexcept { return equal(back, leaves); },
                                    [](DecodeFault) noexcept { return false; });
    image.at(index<3>()) = std::byte{2};
    bool const is_refused =
        decode<Leaves>(read.view())
            .match([](Leaves) noexcept { return false; },
                   [](DecodeFault fault) noexcept { return fault == DecodeFault{DecodeRefusal::bool_byte, 3}; });
    return is_back && is_refused;
}
static_assert(reads_the_image());

// The hash is a function of the value alone, so a constant evaluation gives
// the value that each target gives.  A local part takes no part in it.
[[nodiscard]] consteval bool hash_skips_the_local_part() {
    HoldsLocalPart with_handle{};
    with_handle.handle = Unmarked{7};
    return hash(with_handle) == hash(HoldsLocalPart{}) && !equal(with_handle, HoldsLocalPart{});
}
static_assert(hash_skips_the_local_part());
static_assert(hash(Leaves{}) != hash(Leaves{.word = 1}));

// A plain char hashes by its bits as a value with no sign, so its hash is
// the hash of the unsigned char of the same bits on each target.
struct[[= derive_protocol{}]] HoldsChar {
    char letter = 0;
};

struct[[= derive_protocol{}]] HoldsByte {
    unsigned char letter = 0;
};

static_assert(hash(HoldsChar{static_cast<char>(-1)}).raw() == hash(HoldsByte{0xFF}).raw());

// Each instance of a class template with the annotation carries it.  An
// instance folds as a plain class with the same parts, and an instance with
// a pointer part has no image.
template <class Part>
struct[[= derive_protocol{}]] HoldsPart {
    Part part{};
};

static_assert(Encodable<HoldsPart<std::uint32_t>> && Decodable<HoldsPart<std::uint32_t>>);
static_assert(encoded_size_v<HoldsPart<std::uint32_t>> == 4);
static_assert(hash(HoldsPart<unsigned char>{0xFF}).raw() == hash(HoldsByte{0xFF}).raw());
static_assert(!Encodable<HoldsPart<int const*>>);

// A sum takes one tag byte and room for its largest alternative.  A sum with
// an alternative that has no image has no image.  decode of a sum needs a
// default constructor with no throw and a decode for each alternative.
struct[[= derive_protocol{}]] HoldsSums {
    Option<std::uint32_t> maybe{};
    Result<std::uint16_t, Kind> outcome{std::uint16_t{0}};
    OneOf<std::uint8_t, Leaves> either{std::uint8_t{0}};
};

struct[[= derive_protocol{}]] NoDefault {
    constexpr explicit NoDefault(std::uint32_t value) noexcept : word{value} {}
    std::uint32_t word = 0;
};

static_assert(Encodable<HoldsSums> && Decodable<HoldsSums>);
static_assert(encoded_size_v<HoldsSums> == (1 + 4) + (1 + 2) + (1 + encoded_size_v<Leaves>));
static_assert(!Encodable<HoldsPart<Option<Unmarked>>> && !Encodable<HoldsPart<OneOf<std::uint8_t, Unmarked>>>);
static_assert(Encodable<HoldsPart<Option<NoDefault>>> && !Decodable<HoldsPart<Option<NoDefault>>>);
static_assert(Encodable<HoldsPart<Option<Sealed>>> && !Decodable<HoldsPart<Option<Sealed>>>);

// The image of a sum is its tag and its active alternative, also in a
// constant evaluation.  decode gives the sum back and refuses a tag of no
// alternative at the offset of the tag.
[[nodiscard]] consteval bool reads_the_sums() {
    HoldsSums sums{};
    sums.maybe = Option<std::uint32_t>::some(std::uint32_t{7});
    sums.outcome = Result<std::uint16_t, Kind>{Err<Kind>{Kind::second}};
    sums.either = OneOf<std::uint8_t, Leaves>{Leaves{.word = 3}};
    FixedArray<std::byte, encoded_size_v<HoldsSums>> image{};
    encode(sums, image.view());
    FixedArray<std::byte, encoded_size_v<HoldsSums>> const& read = image;
    bool const is_back = decode<HoldsSums>(read.view())
                             .match([&sums](HoldsSums back) noexcept { return equal(back, sums); },
                                    [](DecodeFault) noexcept { return false; });
    bool const has_tags = image.at(index<0>()) == std::byte{1} && image.at(index<5>()) == std::byte{1}
                       && image.at(index<6>()) == std::byte{1} && image.at(index<8>()) == std::byte{1};
    image.at(index<8>()) = std::byte{2};
    bool const is_refused =
        decode<HoldsSums>(read.view())
            .match([](HoldsSums) noexcept { return false; },
                   [](DecodeFault fault) noexcept { return fault == DecodeFault{DecodeRefusal::sum_tag, 8}; });
    return is_back && has_tags && is_refused && hash(sums) != hash(HoldsSums{});
}
static_assert(reads_the_sums());

}  // namespace detail::protocol_checks

}  // namespace foundation::core
