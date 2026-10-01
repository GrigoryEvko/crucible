#pragma once

// One allocation carries both arrays of an open-addressing table: the control
// bytes first, the slots after them.  Growing means allocating a second buffer
// and assigning over the first, whose destructor frees it.
//
// The slot type must be an implicit-lifetime type throughout, because the
// slots begin their lifetime over the allocated bytes and hold no value until
// the table writes one.  A proof type, or a class that holds one, cannot start
// its lifetime that way, so it cannot be a slot.

#include <foundation/AlignedBuffer.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
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

template <SwissTableSlot SlotPtr>
class [[nodiscard]] SwissTableBuffer {
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
        const size_type rounded = detail::aligned_allocation_bytes<64>(capacity, capacity, sizeof(SlotPtr));
        void* const raw = detail::allocate_aligned_storage_(64, rounded);

        // aligned_alloc creates the control bytes implicitly: an array of a
        // byte type is an implicit-lifetime type.  The slots start their
        // lifetime through the checked start.
        ctrl_type* ctrl = static_cast<ctrl_type*>(raw);
        slot_type* slots = lifetime::start_as_array<slot_type>(static_cast<char*>(raw) + capacity, capacity).data();

        return SwissTableBuffer{raw, ctrl, slots, capacity, rounded};
    }

    SwissTableBuffer(const SwissTableBuffer&) = delete("SwissTableBuffer is move-only");
    SwissTableBuffer& operator=(const SwissTableBuffer&) = delete("SwissTableBuffer is move-only");

    SwissTableBuffer(SwissTableBuffer&& other) noexcept
        : backing_{other.backing_},
          ctrl_{other.ctrl_},
          slots_{other.slots_},
          capacity_{other.capacity_},
          alloc_bytes_{other.alloc_bytes_} {
        other.backing_ = nullptr;
        other.ctrl_ = nullptr;
        other.slots_ = nullptr;
        other.capacity_ = 0;
        other.alloc_bytes_ = 0;
    }

    SwissTableBuffer& operator=(SwissTableBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            backing_ = other.backing_;
            ctrl_ = other.ctrl_;
            slots_ = other.slots_;
            capacity_ = other.capacity_;
            alloc_bytes_ = other.alloc_bytes_;
            other.backing_ = nullptr;
            other.ctrl_ = nullptr;
            other.slots_ = nullptr;
            other.capacity_ = 0;
            other.alloc_bytes_ = 0;
        }
        return *this;
    }

    ~SwissTableBuffer() noexcept { reset(); }

    void reset() noexcept {
        if (backing_ != nullptr) {
            std::free(backing_);
            backing_ = nullptr;
            ctrl_ = nullptr;
            slots_ = nullptr;
            capacity_ = 0;
            alloc_bytes_ = 0;
        }
    }

    // Each array is handed out as a span of `capacity` elements, so a reader
    // holds its bound with its address.
    [[nodiscard]] std::span<ctrl_type> ctrl() noexcept { return {ctrl_, capacity_}; }
    [[nodiscard]] std::span<const ctrl_type> ctrl() const noexcept { return {ctrl_, capacity_}; }
    [[nodiscard]] std::span<slot_type> slots() noexcept { return {slots_, capacity_}; }
    [[nodiscard]] std::span<const slot_type> slots() const noexcept { return {slots_, capacity_}; }

    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_type alloc_bytes() const noexcept { return alloc_bytes_; }
    [[nodiscard]] bool empty() const noexcept { return capacity_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return backing_ != nullptr; }

private:
    explicit SwissTableBuffer(void* backing, ctrl_type* ctrl, slot_type* slots, size_type capacity,
                              size_type alloc_bytes) noexcept
        : backing_{backing}, ctrl_{ctrl}, slots_{slots}, capacity_{capacity}, alloc_bytes_{alloc_bytes} {}

    void* backing_ = nullptr;
    ctrl_type* ctrl_ = nullptr;
    slot_type* slots_ = nullptr;
    size_type capacity_ = 0;
    size_type alloc_bytes_ = 0;
};

namespace detail::swiss_table_buffer_self_test {

// A class that holds a proof, whose lifetime cannot start over bytes.
using ::foundation::lifetime::detail::HoldsProof;

static_assert(!std::is_copy_constructible_v<SwissTableBuffer<void*>>);
static_assert(!std::is_copy_assignable_v<SwissTableBuffer<void*>>);
static_assert(std::is_nothrow_move_constructible_v<SwissTableBuffer<void*>>);
static_assert(std::is_nothrow_move_assignable_v<SwissTableBuffer<void*>>);
static_assert(SwissTableSlot<const int*> && SwissTableSlot<std::uint64_t>);
static_assert(!SwissTableSlot<HoldsProof>, "a slot type whose subobject cannot start its lifetime is refused");
static_assert(!SwissTableSlot<int&>, "no lifetime start binds a reference slot");
static_assert(is_swiss_table_capacity(0) && is_swiss_table_capacity(16) && is_swiss_table_capacity(1024));
static_assert(!is_swiss_table_capacity(8) && !is_swiss_table_capacity(24) && !is_swiss_table_capacity(1)
              && !is_swiss_table_capacity(swiss_table_max_capacity * 2));

}  // namespace detail::swiss_table_buffer_self_test

}  // namespace foundation
