#pragma once

// A value of one class whose parts are the three sums of the base: Option,
// Result and OneOf.  decode reads the untrusted bytes into the class.  An
// oracle reads each byte by the rules of THE SUMS in
// foundation/core/Protocol.h, with no call of decode, and it gives the same
// first fault as decode.  An accepted image is the one image of its value,
// so encode writes the same bytes back, and format writes the value.

#include "../harness.h"

#include <fixy/Core.h>
#include <fixy/Protocol.h>

#include <vector>

namespace crucible::fuzz::boundary {

enum class SumsFault : std::uint8_t {
    first = 1,
    second = 2
};

using SumsEither = ::fixy::OneOf<std::uint8_t, std::uint64_t, bool>;

// The image: the head at 0 and 1, the Option of a word at 2 (its payload at
// 3 to 6), the Result at 7 (its room at 8 and 9), the OneOf at 10 (its room
// at 11 to 18), and the Option of an enumeration at 19 (its room at 20).
struct[[= ::fixy::derive_protocol{}]] SumsValue {
    std::uint16_t head = 0;
    ::fixy::Option<std::uint32_t> maybe{};
    ::fixy::Result<std::uint16_t, SumsFault> outcome{std::uint16_t{0}};
    SumsEither either{std::uint8_t{0}};
    ::fixy::Option<SumsFault> fault{};
};

inline constexpr std::size_t kSumsImageBytes = ::fixy::encoded_size_v<SumsValue>;

using SumsImage = ::foundation::core::FixedArray<std::byte, kSumsImageBytes>;

// The byte at an offset of the image.
[[nodiscard]] inline std::uint8_t sums_byte(SumsImage const& image, std::size_t offset) noexcept {
    return static_cast<std::uint8_t>(
        image.at(::fixy::Index<kSumsImageBytes>::check(offset).expect("the offset is inside the image")));
}

// True when each byte from first up to end is zero.
[[nodiscard]] inline bool are_sums_zero(SumsImage const& image, std::size_t first, std::size_t end) noexcept {
    for (std::size_t offset = first; offset < end; ++offset) {
        if (sums_byte(image, offset) != 0) return false;
    }
    return true;
}

// The first fault of an image in the order of the parts, or none when the
// image holds a value.  Each refusal names the offset of the part: the tag
// of a sum, the first byte after its active alternative, or the leaf.
[[nodiscard]] inline ::fixy::Option<::fixy::DecodeFault> first_sums_fault(SumsImage const& image) noexcept {
    using ::fixy::DecodeRefusal;
    auto const fault_at = [](DecodeRefusal refusal, std::uint32_t offset) noexcept {
        return ::fixy::Option<::fixy::DecodeFault>::some(::fixy::DecodeFault{refusal, offset});
    };
    auto const byte = [&image](std::size_t offset) noexcept { return sums_byte(image, offset); };
    auto const is_fault_byte = [](std::uint8_t raw) noexcept { return raw == 1 || raw == 2; };

    if (byte(2) > 1) return fault_at(DecodeRefusal::sum_tag, 2);
    if (byte(2) == 0 && !are_sums_zero(image, 3, 7)) return fault_at(DecodeRefusal::sum_tail, 3);

    if (byte(7) > 1) return fault_at(DecodeRefusal::sum_tag, 7);
    if (byte(7) == 1 && !is_fault_byte(byte(8))) return fault_at(DecodeRefusal::enumerator, 8);
    if (byte(7) == 1 && byte(9) != 0) return fault_at(DecodeRefusal::sum_tail, 9);

    if (byte(10) > 2) return fault_at(DecodeRefusal::sum_tag, 10);
    if (byte(10) == 2 && byte(11) > 1) return fault_at(DecodeRefusal::bool_byte, 11);
    if (byte(10) != 1 && !are_sums_zero(image, 12, 19)) return fault_at(DecodeRefusal::sum_tail, 12);

    if (byte(19) > 1) return fault_at(DecodeRefusal::sum_tag, 19);
    if (byte(19) == 0 && byte(20) != 0) return fault_at(DecodeRefusal::sum_tail, 20);
    if (byte(19) == 1 && !is_fault_byte(byte(20))) return fault_at(DecodeRefusal::enumerator, 20);
    return ::fixy::none;
}

[[nodiscard]] inline Seeds seeds_protocol_sums() {
    SumsValue full{};
    full.head = 0x0102;
    full.maybe = ::fixy::Option<std::uint32_t>::some(std::uint32_t{0x0A0B0C0D});
    full.outcome = ::fixy::Result<std::uint16_t, SumsFault>{::fixy::Err<SumsFault>{SumsFault::second}};
    full.either = SumsEither{bool{true}};
    full.fault = ::fixy::Option<SumsFault>::some(SumsFault::first);
    Seeds seeds;
    for (SumsValue const& value : {SumsValue{}, full}) {
        SumsImage image{};
        ::fixy::encode(value, image.view());
        std::vector<std::uint8_t> seed;
        for (std::byte const byte : image) {
            seed.push_back(static_cast<std::uint8_t>(byte));
        }
        seeds.push_back(std::move(seed));
    }
    seeds.push_back(std::vector<std::uint8_t>(kSumsImageBytes, 0xFF));
    return seeds;
}

inline void run_protocol_sums(std::span<const std::uint8_t> bytes) {
    SumsImage image{};
    for (auto const index : SumsImage::indices()) {
        if (index.raw() >= bytes.size()) break;
        image.at(index) = static_cast<std::byte>(bytes[index.raw()]);
    }
    SumsImage const& read = image;
    ::fixy::Option<::fixy::DecodeFault> const expected = first_sums_fault(read);
    bool const agrees =
        ::fixy::decode<SumsValue>(read.view())
            .match(
                [&read, &expected](SumsValue value) noexcept {
                    SumsImage again{};
                    ::fixy::encode(value, again.view());
                    ::fixy::FixedText<256> text{};
                    return expected.is_none() && again == read && ::fixy::format(text, value).is_ok();
                },
                [&expected](::fixy::DecodeFault fault) noexcept {
                    return expected.match(
                        [&fault](::fixy::DecodeFault const& oracle) noexcept { return oracle == fault; },
                        []() noexcept { return false; });
                });
    CRUCIBLE_FUZZ_CLAIM("protocol_sums", agrees);
}

}  // namespace crucible::fuzz::boundary
