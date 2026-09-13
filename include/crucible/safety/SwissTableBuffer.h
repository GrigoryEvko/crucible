#pragma once

// One allocation carries both of an open-addressing table's arrays: the control
// bytes first, the slots after them.  Growing means allocating a second buffer
// and assigning over the first, whose destructor frees it.

#include <crucible/Platform.h>
#include <crucible/safety/Decide.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename SlotPtr>
class [[nodiscard]] SwissTableBuffer {
public:
    using ctrl_type = std::int8_t;
    using slot_type = SlotPtr;
    using size_type = std::size_t;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::SwissTableBuffer"; }

    constexpr SwissTableBuffer() noexcept = default;

    // A non-zero capacity must be a power of two, at least the table's group
    // width of 16 and at most 2^30.  Nothing here checks it.  The lower bound is
    // what aligns the slot array: the slots begin at an offset of `capacity`
    // bytes, so a capacity that is a multiple of 16 keeps them 8-byte aligned.
    [[nodiscard]] static SwissTableBuffer allocate(size_type capacity) {
        if (capacity == 0) [[unlikely]]
            return SwissTableBuffer{};
        const size_type slot_bytes = capacity * sizeof(SlotPtr);
        const size_type total = capacity + slot_bytes;
        const size_type rounded = (total + 63) & ~size_type{63};
        void* raw = std::aligned_alloc(64, rounded);
        if (!raw) [[unlikely]]
            std::abort();

        ctrl_type* ctrl = static_cast<ctrl_type*>(raw);
        slot_type* slots = std::start_lifetime_as_array<slot_type>(static_cast<char*>(raw) + capacity, capacity);

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

    [[nodiscard]] ctrl_type* ctrl() noexcept { return ctrl_; }
    [[nodiscard]] const ctrl_type* ctrl() const noexcept { return ctrl_; }
    [[nodiscard]] slot_type* slots() noexcept { return slots_; }
    [[nodiscard]] const slot_type* slots() const noexcept { return slots_; }

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

template <typename S>
SwissTableBuffer(S*, std::size_t) -> SwissTableBuffer<S>;

static_assert(!std::is_copy_constructible_v<SwissTableBuffer<void*>>);
static_assert(std::is_nothrow_move_constructible_v<SwissTableBuffer<void*>>);

}  // namespace crucible::safety
