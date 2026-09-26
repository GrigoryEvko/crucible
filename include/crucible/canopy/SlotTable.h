#pragma once

// The dense, append-only slot table of the canopy batches, plans, digests
// and wire summaries, and the bounded count that guards it.
//
// A bare public count beside a slot array lets a caller store a value
// past the capacity.  Each loop over [0, count) then reads and writes past
// the array, and a size() that refines the count mints a false bound.
// SlotCount keeps the value private, and reserve_next() is the only
// function that moves it.  So `value <= Capacity` is an invariant of the
// type, and a count past the bound is not representable.  The refusal
// occurs at the assignment, before the subscript, and no build flag
// removes it.

#include <fixy/FixedArray.h>
#include <fixy/Refined.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::canopy {

// A slot table holds at least one slot, and its count fits in 16 bits.
template <std::size_t Capacity>
concept SlotCapacity = Capacity > 0 && Capacity <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());

// The bound of the count of a table with Capacity slots: at most Capacity.
template <std::size_t Capacity>
    requires SlotCapacity<Capacity>
inline constexpr auto slot_count_bound = ::fixy::bounded_above<static_cast<std::uint16_t>(Capacity)>;

template <std::size_t Capacity>
    requires SlotCapacity<Capacity>
using BoundedSlotCount = ::fixy::Refined<slot_count_bound<Capacity>, std::uint16_t>;

// The live count of a table with Capacity slots.
template <std::size_t Capacity>
    requires SlotCapacity<Capacity>
class SlotCount {
public:
    // reserve_next() returns the index type of the table that it counts,
    // so FixedArray::at() accepts the index with no conversion.
    using index_type = typename ::fixy::FixedArray<std::byte, Capacity>::index_type;

    constexpr SlotCount() noexcept = default;

    // The conversion is implicit because it cannot lose information: a
    // bounded 16-bit count becomes its own 16-bit value.  No conversion
    // goes the other way, so a number from outside cannot become a count.
    [[nodiscard]] constexpr operator std::uint16_t() const noexcept { return value_; }

    [[nodiscard]] constexpr std::uint16_t value() const noexcept { return value_; }

    [[nodiscard]] constexpr bool full() const noexcept { return value_ >= Capacity; }

    // The count as a refinement.  The checked mint never refuses, because
    // no path gives value_ a number above Capacity.
    [[nodiscard]] constexpr BoundedSlotCount<Capacity> bounded() const noexcept {
        return ::fixy::mint_refined<slot_count_bound<Capacity>>(value_);
    }

    // Reserves the slot after the last live value and returns its index.
    // This is the only mutator.  It refuses at the bound, so each index
    // that it returns is in range.
    [[nodiscard]] constexpr std::optional<index_type> reserve_next() noexcept {
        if (full()) {
            return std::nullopt;
        }
        const auto slot = static_cast<std::size_t>(value_);
        ++value_;
        return ::fixy::mint_refined<::fixy::bounded_above<Capacity - 1>>(slot);
    }

private:
    std::uint16_t value_ = 0;
};

static_assert(sizeof(SlotCount<4>) == sizeof(std::uint16_t));
static_assert(!std::is_assignable_v<SlotCount<4>&, std::uint16_t>, "a count past the bound must stay unrepresentable");
static_assert(!std::is_constructible_v<SlotCount<4>, std::uint16_t>,
              "a count past the bound must stay unrepresentable");

// A dense run of values and its count.  Only slots[0, count) are live,
// and the tail keeps the FixedArray default.  The slots are public, so a
// holder can write a live value.  The count cannot pass Capacity.
template <typename T, std::size_t Capacity>
    requires SlotCapacity<Capacity>
struct SlotTable {
    static constexpr std::size_t capacity = Capacity;

    ::fixy::FixedArray<T, Capacity> slots{};
    SlotCount<Capacity> count{};

    [[nodiscard]] constexpr BoundedSlotCount<Capacity> size() const noexcept { return count.bounded(); }

    [[nodiscard]] constexpr std::span<const T> live() const noexcept {
        return std::span<const T>{slots.data(), static_cast<std::size_t>(count.value())};
    }

    // Appends one value.  When the table is full, it refuses and changes
    // nothing.
    [[nodiscard]] constexpr bool push(T value) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const auto slot = count.reserve_next();
        if (!slot) {
            return false;
        }
        slots.at(*slot) = std::move(value);
        return true;
    }
};

}  // namespace crucible::canopy
