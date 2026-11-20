#pragma once

// One allocation carries both arrays of an open-addressing table: the control
// bytes first, the slots after them.  Growing means allocating a second buffer
// and assigning over the first, whose destructor frees it.
//
// The slot type must be an implicit-lifetime type throughout, because the
// slots begin their lifetime over the allocated bytes and hold no value until
// the table writes one.  A proof type, or a class that holds one, cannot start
// its lifetime that way, so it cannot be a slot.

#include <foundation/AlignedBytes.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace foundation {

// A table groups its control bytes sixteen at a time, and the slot array
// starts `capacity` bytes into the allocation, so a capacity that is a
// multiple of sixteen aligns the slots for any type of alignment sixteen or
// less.
inline constexpr std::size_t swiss_table_group_width = 16;
inline constexpr std::size_t swiss_table_max_capacity = std::size_t{1} << 30;

template <typename SlotPtr>
concept SwissTableSlot = alignof(SlotPtr) <= swiss_table_group_width && lifetime::ImplicitLifetimeThroughout<SlotPtr>;

// True for zero, which is the empty table, and for a power of two in
// [group width, max capacity].
[[nodiscard]] constexpr bool is_swiss_table_capacity(std::size_t capacity) noexcept {
    if (capacity == 0) return true;
    return std::has_single_bit(capacity) && capacity >= swiss_table_group_width && capacity <= swiss_table_max_capacity;
}

namespace core::detail::owner {

// The fields of a SwissTableBuffer: the allocation, the control bytes and the
// slots in it, the capacity and the bytes, or nulls and zeros when it holds
// no allocation.
template <typename SlotPtr>
struct SwissTableBufferState {
    using owner_state_mark = OwnerStateMark;

    // The alignment of the allocation: one cache line for the first group of
    // control bytes.
    static constexpr std::size_t alignment = 64;

    void* backing = nullptr;
    std::int8_t* ctrl = nullptr;
    SlotPtr* slots = nullptr;
    std::size_t capacity = 0;
    std::size_t alloc_bytes = 0;

    [[nodiscard]] static constexpr SwissTableBufferState emptied(SwissTableBufferState const& /*state*/) noexcept {
        return SwissTableBufferState{};
    }

    static constexpr void release(SwissTableBufferState const& state) noexcept {
        if (state.backing != nullptr) {
            heap_release_(state.backing, state.alloc_bytes, alignment);
        }
    }
};

}  // namespace core::detail::owner

// The core of foundation/core/Owner.h gives the moves and the end of a
// buffer.
template <SwissTableSlot SlotPtr>
class [[nodiscard]] SwissTableBuffer final
    : public core::OwnerCore<core::detail::owner::SwissTableBufferState<SlotPtr>> {
    using State = core::detail::owner::SwissTableBufferState<SlotPtr>;
    using Core = core::OwnerCore<State>;

public:
    using ctrl_type = std::int8_t;
    using slot_type = SlotPtr;
    using size_type = std::size_t;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::SwissTableBuffer"; }

    constexpr SwissTableBuffer() noexcept = default;

    // A capacity off the shape misaligns the slots or passes the bound, so
    // the check is an always-on invariant and not a contract clause, which
    // the ignore semantic removes.  A size that wraps aborts, and so does
    // exhaustion, because this runs where a failed allocation has no
    // recovery.
    [[nodiscard]] static SwissTableBuffer allocate(size_type capacity) {
        CRUCIBLE_FATAL_INVARIANT(is_swiss_table_capacity(capacity));
        if (capacity == 0) [[unlikely]]
            return SwissTableBuffer{};
        const size_type rounded = detail::aligned_allocation_bytes<kAlignment>(capacity, capacity, sizeof(SlotPtr));
        void* const raw = core::detail::heap_allocate_(rounded, kAlignment);

        // The allocator creates the control bytes implicitly: an array of a
        // byte type is an implicit-lifetime type.  The slots start their
        // lifetime through the checked start.
        ctrl_type* ctrl = static_cast<ctrl_type*>(raw);
        slot_type* slots = lifetime::start_as_array<slot_type>(static_cast<char*>(raw) + capacity, capacity).data();

        return SwissTableBuffer{raw, ctrl, slots, capacity, rounded};
    }

    SwissTableBuffer(const SwissTableBuffer&) = delete("SwissTableBuffer is move-only");
    SwissTableBuffer& operator=(const SwissTableBuffer&) = delete("SwissTableBuffer is move-only");

    SwissTableBuffer(SwissTableBuffer&&) noexcept = default;
    SwissTableBuffer& operator=(SwissTableBuffer&&) noexcept = default;

    // Gives the allocation back.  The buffer is empty before the release
    // starts.
    void reset() noexcept { this->reset_(); }

    // Each array is handed out as a View of `capacity` elements, so a reader
    // holds its bound with its address.  The View borrows this buffer.  The
    // buffer holds `capacity` control bytes and `capacity` slots, or no
    // allocation and a capacity of zero, so each View comes through the door
    // of the base layer, with no check.
    [[nodiscard]] core::View<ctrl_type> ctrl() & noexcept {
        return core::View<ctrl_type>{core::detail::ViewDoor::key(), this->state_.ctrl, this->state_.capacity};
    }
    [[nodiscard]] core::View<const ctrl_type> ctrl() const& noexcept {
        return core::View<const ctrl_type>{core::detail::ViewDoor::key(), this->state_.ctrl, this->state_.capacity};
    }
    void
    ctrl() && = delete("a view of a temporary SwissTableBuffer points into storage that the full expression frees");
    void ctrl() const&& =
        delete("a view of a temporary SwissTableBuffer points into storage that the full expression frees");

    [[nodiscard]] core::View<slot_type> slots() & noexcept {
        return core::View<slot_type>{core::detail::ViewDoor::key(), this->state_.slots, this->state_.capacity};
    }
    [[nodiscard]] core::View<const slot_type> slots() const& noexcept {
        return core::View<const slot_type>{core::detail::ViewDoor::key(), this->state_.slots, this->state_.capacity};
    }
    void
    slots() && = delete("a view of a temporary SwissTableBuffer points into storage that the full expression frees");
    void slots() const&& =
        delete("a view of a temporary SwissTableBuffer points into storage that the full expression frees");

    [[nodiscard]] size_type capacity() const noexcept { return this->state_.capacity; }
    [[nodiscard]] size_type alloc_bytes() const noexcept { return this->state_.alloc_bytes; }
    [[nodiscard]] bool empty() const noexcept { return this->state_.capacity == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return this->state_.backing != nullptr; }

private:
    static constexpr size_type kAlignment = State::alignment;

    explicit SwissTableBuffer(void* backing, ctrl_type* ctrl, slot_type* slots, size_type capacity,
                              size_type alloc_bytes) noexcept
        : Core{State{backing, ctrl, slots, capacity, alloc_bytes}} {}
};

}  // namespace foundation
