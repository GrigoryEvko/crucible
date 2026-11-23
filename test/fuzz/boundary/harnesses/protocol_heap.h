#pragma once

// The image of a value of run-time size (the heap parts of
// foundation/core/Protocol.h).  The first two bytes of the input give the
// budget of the decode in units of 64 bytes, so the fuzzer reaches the
// refusal of the budget, and the other bytes are the image.  The record holds
// a HeapArray of leaves, a HeapText, a HeapArray of elements whose local part
// makes their storage 512 bytes for an image of three bytes, a HeapArray of
// bounded lists, a HeapArray at an alignment of 64, an enumeration and a pad
// part.
//
// An accepted value is the one value of its image, so encode writes the same
// bytes back, and the storage of its heap parts fits the budget.  A refusal
// names an offset inside the image or at its end.
//
// The interface holds only types of the base (entry.h).

#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

namespace protocol_heap_detail {

enum class Kind : std::uint8_t {
    first = 1,
    second = 2,
};

// An element of three bytes of image and 512 bytes of storage.
struct[[= ::fixy::derive_protocol{}]] Sample {
    std::uint16_t value = 0;
    bool flag = false;
    [[= ::fixy::local_part{}]] ::fixy::FixedArray<std::uint8_t, 509> scratch{};
};

using Tag = ::fixy::FixedList<std::uint8_t, 3>;

// The alignment of the line part.
inline constexpr std::size_t kLineAlign = 64;

struct[[= ::fixy::derive_protocol{}]] Message {
    std::uint32_t id = 0;
    ::fixy::HeapArray<std::uint16_t> values{};
    ::fixy::HeapText name = ::fixy::freeze(::fixy::mint_grow_text(0));
    ::fixy::HeapArray<Sample> samples{};
    ::fixy::HeapArray<Tag> tags{};
    ::fixy::HeapArray<std::uint8_t, kLineAlign> line{};
    Kind kind = Kind::first;
    [[= ::fixy::pad_part{}]] std::uint8_t pad = 0;
};

using Budget = ::fixy::Quantity<::fixy::unit::Byte, std::size_t>;

// The bytes of the budget at the start of each input, and the bytes of one
// unit of the budget.
inline constexpr std::size_t kBudgetBytes = 2;
inline constexpr std::size_t kBudgetUnit = 64;

using Bytes = ::fixy::HeapArray<std::byte>;

// The seed inputs of the harness.
using HeapSeeds = ::fixy::FixedList<Bytes, 4>;

[[nodiscard]] constexpr ::foundation::effects::Alloc alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The storage that decode allocates for the heap parts of value: the count
// of each array times the size of its element, rounded up to the alignment
// of the array, and the count of the characters of the text.
[[nodiscard]] inline std::size_t storage_of(Message const& value) noexcept {
    std::size_t const line_bytes = (value.line.size() + kLineAlign - 1) / kLineAlign * kLineAlign;
    return value.values.size() * sizeof(std::uint16_t) + value.name.size() + value.samples.size() * sizeof(Sample)
         + value.tags.size() * sizeof(Tag) + line_bytes;
}

// An input: the budget in units, rounded up, and then the image of value.
// The units of the budget fit in the two bytes.
[[nodiscard]] inline Bytes input_of(Budget budget, Message const& value) noexcept {
    std::size_t const units = (budget.raw() + kBudgetUnit - 1) / kBudgetUnit;
    CRUCIBLE_FATAL_INVARIANT(units <= 0xFFFFU);
    std::size_t const size = ::fixy::encoded_size(value);
    Bytes input = ::fixy::mint_heap_array_zeroed<std::byte>(alloc(), kBudgetBytes + size);
    input = ::fixy::with_mut(std::move(input), [&value, units, size](auto run) noexcept {
        auto low = run.get_mut(0).expect("the input holds the budget");
        *low = static_cast<std::byte>(units & 0xFFU);
        auto high = run.get_mut(1).expect("the input holds the budget");
        *high = static_cast<std::byte>(units >> 8);
        ::fixy::encode(value,
                       ::fixy::take_window(std::move(run), kBudgetBytes, size).expect("the input holds the image"));
    });
    return input;
}

// The text of the name of the full message.  It comes out as a value, so a
// freeze takes it with no cast.
[[nodiscard]] inline ::fixy::GrowText grown_name() noexcept {
    ::fixy::GrowText name = ::fixy::mint_grow_text(4);
    bool const is_named = name.append(alloc(), ::fixy::TextView{"word"}).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_named);
    return name;
}

[[nodiscard]] inline Message full_message() noexcept {
    Message value{};
    value.id = 0x0A0B0C0DU;
    value.values = ::fixy::mint_heap_array_with<std::uint16_t>(
        alloc(), 3, [](std::size_t position) noexcept { return static_cast<std::uint16_t>(position * 7); });
    value.name = ::fixy::freeze(grown_name());
    value.samples = ::fixy::mint_heap_array_with<Sample>(alloc(), 2, [](std::size_t position) noexcept {
        Sample sample{};
        sample.value = static_cast<std::uint16_t>(position + 1);
        sample.flag = position == 0;
        return sample;
    });
    value.tags = ::fixy::mint_heap_array_with<Tag>(alloc(), 2, [](std::size_t position) noexcept {
        Tag tag{};
        if (tag.append(static_cast<std::uint8_t>(position)).is_err()) ::fixy::unreachable();
        return tag;
    });
    value.line = ::fixy::mint_heap_array_with<std::uint8_t, kLineAlign>(
        alloc(), 5, [](std::size_t position) noexcept { return static_cast<std::uint8_t>(position * 3); });
    value.kind = Kind::second;
    return value;
}

}  // namespace protocol_heap_detail

[[nodiscard]] inline protocol_heap_detail::HeapSeeds seeds_protocol_heap() noexcept {
    namespace detail = protocol_heap_detail;
    detail::Message const full = detail::full_message();
    detail::HeapSeeds seeds{};
    bool const is_built = seeds.append(detail::input_of(detail::Budget{0}, detail::Message{})).is_ok()
                       && seeds.append(detail::input_of(detail::Budget{detail::storage_of(full)}, full)).is_ok()
                       && seeds.append(detail::input_of(detail::Budget{0xFFFFU * detail::kBudgetUnit}, full)).is_ok()
                       && seeds.append(detail::input_of(detail::Budget{detail::kBudgetUnit}, full)).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    return seeds;
}

inline void run_protocol_heap(::fixy::View<std::byte const> input) noexcept {
    namespace detail = protocol_heap_detail;
    std::size_t units = 0;
    for (std::size_t at = 0; at < detail::kBudgetBytes && at < input.size(); ++at) {
        units |= static_cast<std::size_t>(*input.get(at).expect("the input holds the byte")) << (8 * at);
    }
    std::size_t const head = input.size() < detail::kBudgetBytes ? input.size() : detail::kBudgetBytes;
    ::fixy::View<std::byte const> const image =
        input.window(head, input.size() - head).expect("the image is the input after the budget");
    detail::Budget const budget{units * detail::kBudgetUnit};
    ::fixy::decode<detail::Message>(detail::alloc(), budget, image)
        .match(
            [&image, budget](detail::Message value) noexcept {
                CRUCIBLE_FATAL_INVARIANT(::fixy::encoded_size(value) == image.size());
                CRUCIBLE_FATAL_INVARIANT(detail::storage_of(value) <= budget.raw());
                detail::Bytes again = ::fixy::mint_heap_array_zeroed<std::byte>(detail::alloc(), image.size());
                again = ::fixy::with_mut(std::move(again),
                                         [&value](auto run) noexcept { ::fixy::encode(value, std::move(run)); });
                detail::Bytes const& written = again;
                CRUCIBLE_FATAL_INVARIANT(::fixy::equal(written.view(), image));
            },
            [&image](::fixy::DecodeFault fault) noexcept { CRUCIBLE_FATAL_INVARIANT(fault.offset() <= image.size()); });
}

}  // namespace crucible::fuzz::boundary
