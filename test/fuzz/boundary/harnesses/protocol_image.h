#pragma once

// The image of a value of fixed size (THE CODEC of foundation/core/Protocol.h).
// The harness reads the first bytes of the input as the image of a record
// whose parts hold each kind of part of the family: the leaves, a pad part, a
// base, a FixedArray, the three sums and the two bounded lists.  A short
// input gets zero bytes after its end.  An accepted value is the one value of
// its image, so encode writes the same bytes back, and its format fits a
// text of 2048 characters.  A refusal names an offset inside the image.
//
// The interface holds only types of the base (entry.h).

#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

namespace protocol_image_detail {

enum class Kind : std::uint8_t {
    first = 1,
    second = 2,
    seventh = 7,
};

struct[[= ::fixy::derive_protocol{}]] Leaves {
    std::uint32_t word = 0;
    bool flag = false;
    Kind kind = Kind::first;
    [[= ::fixy::pad_part{}]] std::uint8_t pad = 0;
    double real = 0.0;
    std::int16_t signed_word = 0;
    char letter = 0;
};

struct[[= ::fixy::derive_protocol{}]] Record : Leaves {
    ::fixy::FixedArray<std::uint16_t, 3> triple{};
    ::fixy::Option<std::uint32_t> maybe{};
    ::fixy::Result<std::uint16_t, Kind> outcome{std::uint16_t{0}};
    ::fixy::OneOf<std::uint8_t, std::uint64_t, Leaves> either{std::uint8_t{0}};
    ::fixy::FixedList<Leaves, 3> leaves{};
    ::fixy::FixedText<12> name{};
};

inline constexpr std::size_t kImageBytes = ::fixy::encoded_size_v<Record>;

using Image = ::fixy::FixedArray<std::byte, kImageBytes>;

// The seed images of the harness.
using ImageSeeds = ::fixy::FixedList<Image, 3>;

[[nodiscard]] inline Image image_of(Record const& value) noexcept {
    Image image{};
    ::fixy::encode(value, image.view());
    return image;
}

[[nodiscard]] inline Record full_record() noexcept {
    Record value{};
    value.word = 0x01020304U;
    value.flag = true;
    value.kind = Kind::seventh;
    value.real = -2.5;
    value.signed_word = -3;
    value.letter = 'q';
    value.triple = ::fixy::FixedArray<std::uint16_t, 3>{1, 2, 3};
    value.maybe = ::fixy::Option<std::uint32_t>::some(std::uint32_t{9});
    value.outcome = ::fixy::Result<std::uint16_t, Kind>{::fixy::Err<Kind>{Kind::second}};
    value.either = ::fixy::OneOf<std::uint8_t, std::uint64_t, Leaves>{Leaves{}};
    bool const is_built = value.leaves.append(Leaves{}).is_ok() && value.name.append(::fixy::TextView{"a\"b"}).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    return value;
}

}  // namespace protocol_image_detail

[[nodiscard]] inline protocol_image_detail::ImageSeeds seeds_protocol_image() noexcept {
    namespace detail = protocol_image_detail;
    detail::Record other = detail::full_record();
    other.either = ::fixy::OneOf<std::uint8_t, std::uint64_t, detail::Leaves>{std::uint64_t{0xFFFFFFFFFFFFFFFFULL}};
    other.maybe = ::fixy::Option<std::uint32_t>{::fixy::none};
    detail::ImageSeeds seeds{};
    bool const is_built = seeds.append(detail::image_of(detail::Record{})).is_ok()
                       && seeds.append(detail::image_of(detail::full_record())).is_ok()
                       && seeds.append(detail::image_of(other)).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    return seeds;
}

inline void run_protocol_image(::fixy::View<std::byte const> input) noexcept {
    namespace detail = protocol_image_detail;
    detail::Image bytes{};
    std::size_t const taken = input.size() < detail::kImageBytes ? input.size() : detail::kImageBytes;
    ::fixy::copy(::fixy::View<std::byte>{bytes.view()}.window(0, taken).expect("the image holds the bytes it takes"),
                 input.window(0, taken).expect("the input holds the bytes that the image takes"))
        .expect("the two windows hold the same count");
    detail::Image const& read = bytes;
    ::fixy::decode<detail::Record>(read.view())
        .match(
            [&read](detail::Record value) noexcept {
                detail::Image const again = detail::image_of(value);
                CRUCIBLE_FATAL_INVARIANT(::fixy::equal(again.view(), read.view()));
                ::fixy::FixedText<2048> text{};
                bool const is_written = ::fixy::format(text, value).is_ok();
                CRUCIBLE_FATAL_INVARIANT(is_written);
            },
            [](::fixy::DecodeFault fault) noexcept { CRUCIBLE_FATAL_INVARIANT(fault.offset() < detail::kImageBytes); });
}

}  // namespace crucible::fuzz::boundary
