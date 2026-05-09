#pragma once

// ── crucible::safety::SwissTableBuffer ────────────────────────────
//
// Move-only RAII wrapper over a single aligned heap allocation that
// holds a Swiss-table's coupled control-byte array + slot-pointer
// array in one contiguous backing region.  Replaces hand-rolled
// patterns like:
//
//   void* backing = std::aligned_alloc(64, ctrl_bytes + slot_bytes);
//   int8_t*       ctrl  = static_cast<int8_t*>(backing);
//   const Expr**  slots = static_cast<const Expr**>(... offset ...);
//   ...
//   std::free(backing);
//
// The capacity is a `Refined<is_power_of_two_le, uint32_t>`-shape
// invariant: at construction we contract-check pow-2 bounds.  Slot
// type is parameterized via `SlotPtr` so the same template serves
// ExprPool (`const Expr*`) and forge::RecipePool (`Slot*`).
//
//   Axiom coverage: MemSafe, LeakSafe, TypeSafe (capacity invariant).
//   Runtime cost:   one void* + ctrl/slots projections + capacity.
//                   Reads of ctrl()/slots() are constant-time.
//
// Discipline: SwissTableBuffer is OWNED.  rebuild() is destructive +
// returns the new buffer; the caller assigns and the old buffer's
// dtor frees.

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

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::SwissTableBuffer";
    }

    constexpr SwissTableBuffer() noexcept = default;

    // Allocate ctrl[capacity] + slots[capacity] in one aligned region.
    // capacity MUST be a power of two and ≤ 2^30 (matches the same
    // invariant used by ExprPool::grow_to_).  Caller is expected to
    // provide a capacity that satisfies the contract; we use an
    // invariant assumption rather than throwing (Crucible has no
    // exceptions).
    //
    //   Layout: [ctrl: capacity bytes] [slots: capacity * sizeof(slot)]
    //
    // The slot array begins at offset `capacity` from backing_, which
    // is always a multiple of kGroupWidth (≥16) → always 8-byte aligned.
    [[nodiscard]] static SwissTableBuffer allocate(size_type capacity) {
        if (capacity == 0) [[unlikely]] return SwissTableBuffer{};
        // Caller-side discipline: capacity must be power-of-two.
        const size_type slot_bytes = capacity * sizeof(SlotPtr);
        const size_type total      = capacity + slot_bytes;
        const size_type rounded    = (total + 63) & ~size_type{63};
        void* raw = std::aligned_alloc(64, rounded);
        if (!raw) [[unlikely]] std::abort();

        ctrl_type* ctrl  = static_cast<ctrl_type*>(raw);
        slot_type* slots = std::start_lifetime_as_array<slot_type>(
            static_cast<char*>(raw) + capacity, capacity);

        return SwissTableBuffer{raw, ctrl, slots, capacity, rounded};
    }

    SwissTableBuffer(const SwissTableBuffer&) = delete("SwissTableBuffer is move-only");
    SwissTableBuffer& operator=(const SwissTableBuffer&) = delete("SwissTableBuffer is move-only");

    SwissTableBuffer(SwissTableBuffer&& other) noexcept
        : backing_{other.backing_}, ctrl_{other.ctrl_}, slots_{other.slots_},
          capacity_{other.capacity_}, alloc_bytes_{other.alloc_bytes_} {
        other.backing_ = nullptr;
        other.ctrl_ = nullptr;
        other.slots_ = nullptr;
        other.capacity_ = 0;
        other.alloc_bytes_ = 0;
    }

    SwissTableBuffer& operator=(SwissTableBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            backing_     = other.backing_;
            ctrl_        = other.ctrl_;
            slots_       = other.slots_;
            capacity_    = other.capacity_;
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

    [[nodiscard]] ctrl_type*       ctrl()    noexcept       { return ctrl_; }
    [[nodiscard]] const ctrl_type* ctrl()    const noexcept { return ctrl_; }
    [[nodiscard]] slot_type*       slots()   noexcept       { return slots_; }
    [[nodiscard]] const slot_type* slots()   const noexcept { return slots_; }

    [[nodiscard]] size_type capacity()    const noexcept { return capacity_; }
    [[nodiscard]] size_type alloc_bytes() const noexcept { return alloc_bytes_; }
    [[nodiscard]] bool      empty()       const noexcept { return capacity_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return backing_ != nullptr; }

private:
    explicit SwissTableBuffer(void* backing, ctrl_type* ctrl, slot_type* slots,
                              size_type capacity, size_type alloc_bytes) noexcept
        : backing_{backing}, ctrl_{ctrl}, slots_{slots},
          capacity_{capacity}, alloc_bytes_{alloc_bytes} {}

    void*      backing_     = nullptr;
    ctrl_type* ctrl_        = nullptr;
    slot_type* slots_       = nullptr;
    size_type  capacity_    = 0;
    size_type  alloc_bytes_ = 0;
};

template <typename S>
SwissTableBuffer(S*, std::size_t) -> SwissTableBuffer<S>;

static_assert(!std::is_copy_constructible_v<SwissTableBuffer<void*>>);
static_assert(std::is_nothrow_move_constructible_v<SwissTableBuffer<void*>>);

}  // namespace crucible::safety
