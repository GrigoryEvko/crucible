#pragma once

// A value of one class whose parts are the three sums of the base (Option,
// Result and OneOf) and its two bounded lists (FixedList and FixedText).
// decode reads the untrusted bytes into the class.  An oracle reads each byte
// by the rules of THE SUMS and THE BOUNDED LISTS in
// foundation/core/Protocol.h, with no call of decode, and it gives the same
// first fault as decode.  An accepted image is the one image of its value,
// so encode writes the same bytes back, and format writes the value.

#include "../harness.h"

#include <fixy/Core.h>
#include <fixy/Protocol.h>

#include <vector>

namespace crucible::fuzz::boundary {

enum class ImageFault : std::uint8_t {
    first = 1,
    second = 2
};

using ImageEither = ::fixy::OneOf<std::uint8_t, std::uint64_t, bool>;

// The image: the head at 0 and 1, the Option of a word at 2 (its payload at
// 3 to 6), the Result at 7 (its room at 8 and 9), the OneOf at 10 (its room
// at 11 to 18), the Option of an enumeration at 19 (its room at 20), the list
// of words at 21 (its rooms at 22 to 25), and the text at 26 (its rooms at 27
// to 29).
struct[[= ::fixy::derive_protocol{}]] ImageValue {
    std::uint16_t head = 0;
    ::fixy::Option<std::uint32_t> maybe{};
    ::fixy::Result<std::uint16_t, ImageFault> outcome{std::uint16_t{0}};
    ImageEither either{std::uint8_t{0}};
    ::fixy::Option<ImageFault> fault{};
    ::fixy::FixedList<std::uint16_t, 2> words{};
    ::fixy::FixedText<3> name{};
};

inline constexpr std::size_t kImageBytes = ::fixy::encoded_size_v<ImageValue>;

using ImageBytes = ::foundation::core::FixedArray<std::byte, kImageBytes>;

// The byte at an offset of the image.
[[nodiscard]] inline std::uint8_t image_byte(ImageBytes const& image, std::size_t offset) noexcept {
    return static_cast<std::uint8_t>(
        image.at(::fixy::Index<kImageBytes>::check(offset).expect("the offset is inside the image")));
}

// True when each byte from first up to end is zero.
[[nodiscard]] inline bool are_image_zero(ImageBytes const& image, std::size_t first, std::size_t end) noexcept {
    for (std::size_t offset = first; offset < end; ++offset) {
        if (image_byte(image, offset) != 0) return false;
    }
    return true;
}

// The first fault of an image in the order of the parts, or none when the
// image holds a value.  Each refusal names the offset of the part: the tag
// of a sum, the count of a list, the first byte after the active part, or
// the leaf.
[[nodiscard]] inline ::fixy::Option<::fixy::DecodeFault> first_image_fault(ImageBytes const& image) noexcept {
    using ::fixy::DecodeRefusal;
    auto const fault_at = [](DecodeRefusal refusal, std::size_t offset) noexcept {
        return ::fixy::Option<::fixy::DecodeFault>::some(
            ::fixy::DecodeFault{refusal, static_cast<std::uint32_t>(offset)});
    };
    auto const byte = [&image](std::size_t offset) noexcept { return image_byte(image, offset); };
    auto const is_fault_byte = [](std::uint8_t raw) noexcept { return raw == 1 || raw == 2; };

    if (byte(2) > 1) return fault_at(DecodeRefusal::sum_tag, 2);
    if (byte(2) == 0 && !are_image_zero(image, 3, 7)) return fault_at(DecodeRefusal::tail_byte, 3);

    if (byte(7) > 1) return fault_at(DecodeRefusal::sum_tag, 7);
    if (byte(7) == 1 && !is_fault_byte(byte(8))) return fault_at(DecodeRefusal::enumerator, 8);
    if (byte(7) == 1 && byte(9) != 0) return fault_at(DecodeRefusal::tail_byte, 9);

    if (byte(10) > 2) return fault_at(DecodeRefusal::sum_tag, 10);
    if (byte(10) == 2 && byte(11) > 1) return fault_at(DecodeRefusal::bool_byte, 11);
    if (byte(10) != 1 && !are_image_zero(image, 12, 19)) return fault_at(DecodeRefusal::tail_byte, 12);

    if (byte(19) > 1) return fault_at(DecodeRefusal::sum_tag, 19);
    if (byte(19) == 0 && byte(20) != 0) return fault_at(DecodeRefusal::tail_byte, 20);
    if (byte(19) == 1 && !is_fault_byte(byte(20))) return fault_at(DecodeRefusal::enumerator, 20);

    std::size_t const word_end = 22 + 2 * std::size_t{byte(21)};
    if (byte(21) > 2) return fault_at(DecodeRefusal::list_count, 21);
    if (!are_image_zero(image, word_end, 26)) return fault_at(DecodeRefusal::tail_byte, word_end);

    std::size_t const name_end = 27 + std::size_t{byte(26)};
    if (byte(26) > 3) return fault_at(DecodeRefusal::list_count, 26);
    if (!are_image_zero(image, name_end, 30)) return fault_at(DecodeRefusal::tail_byte, name_end);
    return ::fixy::none;
}

[[nodiscard]] inline Seeds seeds_protocol_image() {
    ImageValue full{};
    full.head = 0x0102;
    full.maybe = ::fixy::Option<std::uint32_t>::some(std::uint32_t{0x0A0B0C0D});
    full.outcome = ::fixy::Result<std::uint16_t, ImageFault>{::fixy::Err<ImageFault>{ImageFault::second}};
    full.either = ImageEither{bool{true}};
    full.fault = ::fixy::Option<ImageFault>::some(ImageFault::first);
    bool const is_built = full.words.append(std::uint16_t{0x0102}).is_ok()
                       && full.words.append(std::uint16_t{0x0304}).is_ok()
                       && full.name.append(::fixy::TextView{"a\""}).is_ok();
    CRUCIBLE_FUZZ_CLAIM("protocol_image", is_built);
    Seeds seeds;
    for (ImageValue const& value : {ImageValue{}, full}) {
        ImageBytes image{};
        ::fixy::encode(value, image.view());
        std::vector<std::uint8_t> seed;
        for (std::byte const byte : image) {
            seed.push_back(static_cast<std::uint8_t>(byte));
        }
        seeds.push_back(std::move(seed));
    }
    seeds.push_back(std::vector<std::uint8_t>(kImageBytes, 0xFF));
    return seeds;
}

inline void run_protocol_image(std::span<const std::uint8_t> bytes) {
    ImageBytes image{};
    for (auto const index : ImageBytes::indices()) {
        if (index.raw() >= bytes.size()) break;
        image.at(index) = static_cast<std::byte>(bytes[index.raw()]);
    }
    ImageBytes const& read = image;
    ::fixy::Option<::fixy::DecodeFault> const expected = first_image_fault(read);
    bool const agrees =
        ::fixy::decode<ImageValue>(read.view())
            .match(
                [&read, &expected](ImageValue value) noexcept {
                    ImageBytes again{};
                    ::fixy::encode(value, again.view());
                    ::fixy::FixedText<256> text{};
                    return expected.is_none() && again == read && ::fixy::format(text, value).is_ok();
                },
                [&expected](::fixy::DecodeFault fault) noexcept {
                    return expected.match(
                        [&fault](::fixy::DecodeFault const& oracle) noexcept { return oracle == fault; },
                        []() noexcept { return false; });
                });
    CRUCIBLE_FUZZ_CLAIM("protocol_image", agrees);
}

}  // namespace crucible::fuzz::boundary
