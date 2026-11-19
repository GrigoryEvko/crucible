#pragma once

// A value of one class with heap parts of the base (a HeapArray of words, a
// HeapText and a HeapArray of records) and parts of fixed size.  decode
// reads the untrusted bytes, of any length, into the class through the
// allocation capability.  An oracle reads each byte by the rules of THE
// HEAP PARTS in foundation/core/Protocol.h, with no call of decode, and it
// gives the same first fault as decode.  A count that needs more bytes than
// remain is refused before the allocation.  An accepted value is the one
// value of its bytes: encode writes the same bytes back, and its hash is the
// hand fold of the bytes and their count.

#include "../harness.h"

#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <foundation/Quarantine.h>

#include <vector>

namespace crucible::fuzz::boundary {

enum class HeapKind : std::uint8_t {
    first = 1,
    second = 2
};

struct[[= ::fixy::derive_protocol{}]] HeapPoint {
    std::uint16_t x = 0;
    bool flag = false;
};

// The image: id in 4 bytes, the count of words in 8 bytes and two bytes for
// each word, the count of the name and one byte for each character, the
// count of points and three bytes for each point, and then kind.
struct[[= ::fixy::derive_protocol{}]] HeapValue {
    std::uint32_t id = 0;
    ::fixy::HeapArray<std::uint16_t> words{};
    ::foundation::core::HeapText name = ::foundation::core::freeze(::fixy::mint_grow_text(0));
    ::fixy::HeapArray<HeapPoint> points{};
    HeapKind kind = HeapKind::first;
};

// The little-endian word of width bytes from at.  The caller makes sure
// that the bytes lie in the input.
[[nodiscard]] inline std::uint64_t heap_word(std::span<const std::uint8_t> bytes, std::size_t at,
                                             std::size_t width) noexcept {
    std::uint64_t word = 0;
    for (std::size_t index = 0; index < width; ++index) {
        word |= static_cast<std::uint64_t>(bytes[at + index]) << (8 * index);
    }
    return word;
}

// The first fault of the bytes in the order of the parts, or none when the
// bytes hold a value.  A part that the bytes cut names the offset of its
// first byte.
[[nodiscard]] inline ::fixy::Option<::fixy::DecodeFault>
first_heap_fault(std::span<const std::uint8_t> bytes) noexcept {
    using ::fixy::DecodeRefusal;
    auto const fault_at = [](DecodeRefusal refusal, std::size_t offset) noexcept {
        return ::fixy::Option<::fixy::DecodeFault>::some(
            ::fixy::DecodeFault{refusal, static_cast<std::uint32_t>(offset)});
    };
    std::size_t const size = bytes.size();
    std::size_t at = 0;
    // The count of a heap part of step bytes for each element, or the size
    // of the input when the count needs more bytes than remain.
    auto const count_at = [&bytes, size](std::size_t from, std::size_t step) noexcept -> std::uint64_t {
        std::uint64_t const count = heap_word(bytes, from, 8);
        return count > (size - from - 8) / step ? std::uint64_t{size} + 1 : count;
    };

    if (size - at < 4) return fault_at(DecodeRefusal::image_end, at);
    at += 4;

    if (size - at < 8) return fault_at(DecodeRefusal::image_end, at);
    std::uint64_t const words = count_at(at, 2);
    if (words > size) return fault_at(DecodeRefusal::image_end, at);
    at += 8 + 2 * words;

    if (size - at < 8) return fault_at(DecodeRefusal::image_end, at);
    std::uint64_t const characters = count_at(at, 1);
    if (characters > size) return fault_at(DecodeRefusal::image_end, at);
    at += 8 + characters;

    if (size - at < 8) return fault_at(DecodeRefusal::image_end, at);
    std::uint64_t const points = count_at(at, 3);
    if (points > size) return fault_at(DecodeRefusal::image_end, at);
    at += 8;
    for (std::uint64_t index = 0; index < points; ++index) {
        if (bytes[at + 2] > 1) return fault_at(DecodeRefusal::bool_byte, at + 2);
        at += 3;
    }

    if (size - at < 1) return fault_at(DecodeRefusal::image_end, at);
    if (bytes[at] != 1 && bytes[at] != 2) return fault_at(DecodeRefusal::enumerator, at);
    at += 1;
    if (at != size) return fault_at(DecodeRefusal::trailing_byte, at);
    return ::fixy::none;
}

// The hand fold of the hash of an image of run-time size: blocks of 16
// bytes, the last one filled with zero bytes, and then one block with the
// count of the bytes.
[[nodiscard]] inline std::uint64_t hand_heap_fold(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::uint64_t fold_seed = 0x243f6a8885a308d3ULL;
    auto const fold = [](std::uint64_t left, std::uint64_t right) noexcept {
        ::foundation::core::WideProduct<std::uint64_t> const product = ::foundation::core::widening_mul(left, right);
        return product.low ^ product.high;
    };
    std::uint64_t accumulator = 0x13198a2e03707344ULL;
    for (std::size_t first = 0; first < bytes.size(); first += 16) {
        std::uint64_t low = 0;
        std::uint64_t high = 0;
        for (std::size_t lane = 0; lane < 16 && first + lane < bytes.size(); ++lane) {
            std::uint64_t const byte = bytes[first + lane];
            if (lane < 8) {
                low |= byte << (8 * lane);
            } else {
                high |= byte << (8 * (lane - 8));
            }
        }
        accumulator = fold(low ^ accumulator, high ^ fold_seed);
    }
    std::uint64_t const length = bytes.size();
    return fold(length ^ accumulator, fold_seed);
}

[[nodiscard]] inline Seeds seeds_protocol_heap() {
    HeapValue full{};
    full.id = 0x01020304U;
    full.words = ::fixy::mint_heap_array_with<std::uint16_t>(
        test_alloc(), 3, [](std::size_t position) noexcept { return static_cast<std::uint16_t>(0x0101 * position); });
    ::foundation::core::GrowText name = ::fixy::mint_grow_text(8);
    bool const is_put = name.append(test_alloc(), ::fixy::TextView{"a\"b"}).is_ok();
    CRUCIBLE_FUZZ_CLAIM("protocol_heap", is_put);
    full.name = ::foundation::core::freeze(std::move(name));
    full.points = ::fixy::mint_heap_array_with<HeapPoint>(test_alloc(), 2, [](std::size_t position) noexcept {
        return HeapPoint{static_cast<std::uint16_t>(position + 1), position == 0};
    });
    full.kind = HeapKind::second;
    Seeds seeds;
    auto const add_seed = [&seeds](HeapValue const& value) {
        ::fixy::HeapArray<std::byte> image =
            ::fixy::mint_heap_array_zeroed<std::byte>(test_alloc(), ::fixy::encoded_size(value));
        ::fixy::encode(value, image.view());
        std::vector<std::uint8_t> seed;
        for (std::byte const byte : image) {
            seed.push_back(static_cast<std::uint8_t>(byte));
        }
        seeds.push_back(std::move(seed));
    };
    add_seed(full);
    add_seed(HeapValue{});
    return seeds;
}

inline void run_protocol_heap(std::span<const std::uint8_t> bytes) {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fuzzer gives its input as a pointer to raw bytes and a count")
    std::span<const std::byte> const raw = as_bytes_view(bytes);
    ::fixy::View<std::byte const> const input = ::fixy::mint_foreign_view(raw.data(), raw.size());
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    ::fixy::Option<::fixy::DecodeFault> const expected = first_heap_fault(bytes);
    bool const agrees =
        ::fixy::decode<HeapValue>(test_alloc(), input)
            .match(
                [&input, &bytes, &expected](HeapValue value) noexcept {
                    if (expected.is_some() || ::fixy::encoded_size(value) != input.size()) return false;
                    ::fixy::HeapArray<std::byte> again =
                        ::fixy::mint_heap_array_zeroed<std::byte>(test_alloc(), input.size());
                    ::fixy::encode(value, again.view());
                    ::fixy::HeapArray<std::byte> const& read = again;
                    // The text of a long value does not fit, so the format
                    // gives either result, and the run only reads it.
                    ::fixy::FixedText<64> text{};
                    [[maybe_unused]] bool const is_formatted = ::fixy::format(text, value).is_ok();
                    return ::fixy::equal(read.view(), input) && ::fixy::hash(value).raw() == hand_heap_fold(bytes);
                },
                [&expected](::fixy::DecodeFault fault) noexcept {
                    return expected.match(
                        [&fault](::fixy::DecodeFault const& oracle) noexcept { return oracle == fault; },
                        []() noexcept { return false; });
                });
    CRUCIBLE_FUZZ_CLAIM("protocol_heap", agrees);
}

}  // namespace crucible::fuzz::boundary
