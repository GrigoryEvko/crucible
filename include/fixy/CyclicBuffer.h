#pragma once

// Bounded ring buffer that keeps the last N elements, evicting the oldest on
// overflow, and reads most recent first.
//
// The fill count is a separate saturating counter rather than
// min(cursor.raw(), N).  The cursor is free-running and wraps at 2^bits, so
// once it laps, `raw < N` becomes true again and a derived size would
// under-report a full ring.

#include <fixy/Core.h>
#include <fixy/Cyclic.h>
#include <fixy/Mutation.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename T, std::size_t N>
    requires(N > 0 && (N & (N - 1)) == 0)
class [[nodiscard]] CyclicBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = T const&;

    static constexpr size_type capacity = N;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::CyclicBuffer"; }

private:
    // The slot of a masked cursor.  The mask of the cursor keeps it below N,
    // so the optimizer removes the check.
    [[nodiscard]] static constexpr Index<N> prove_slot_(size_type masked) noexcept {
        return Index<N>::check(masked).expect("a masked cursor of a CyclicBuffer is below its capacity");
    }

    FixedArray<T, N> storage_{};
    Cyclic<std::size_t, N> cursor_{};
    BoundedMonotonic<std::size_t, N> count_ = mint_bounded_monotonic<std::size_t, N>(std::size_t{0});

    // The slot at a proven index, for a write.  The ring gives its slots by
    // reference, and a FixedArray gives a View that writes only through the
    // keyed run of a frame, so the ring reaches its own storage through it.
    [[nodiscard]] constexpr reference slot_at_(Index<N> slot) noexcept {
        return frame_run_(::foundation::core::detail::FrameDoor::key(), storage_).at(slot);
    }

public:
    constexpr CyclicBuffer() = default;

    constexpr CyclicBuffer(CyclicBuffer const&) = default;
    constexpr CyclicBuffer(CyclicBuffer&&) = default;
    constexpr CyclicBuffer& operator=(CyclicBuffer const&) = default;
    constexpr CyclicBuffer& operator=(CyclicBuffer&&) = default;
    ~CyclicBuffer() = default;

    [[nodiscard]] constexpr size_type size() const noexcept { return count_.get(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_.get() == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return count_.get() == N; }

    // The returned slot still holds its prior value, and the caller
    // overwrites it.  The reference stays valid until N more claims wrap
    // back onto the same slot.
    //
    // The order is bind at the current index, then advance.  advance()
    // touches only the cursor, so the reference into the storage survives it.
    //
    // The fill guard is load-bearing, not defensive.  bump() does not
    // saturate: its precondition is that the count is below the bound, and
    // a full ring would break it.
    [[nodiscard]] constexpr reference claim() noexcept {
        reference slot = slot_at_(prove_slot_(cursor_.index()));
        cursor_.advance();
        if (count_.get() < N) count_.bump();
        return slot;
    }

    // The slot of the next claim, with a value-initialized element in it.
    // The claim ends the old element and starts the new one in the same
    // storage, so a ring of elements that refuse an assignment, such as an
    // element with a counter that only goes up, recycles its slots.  The new
    // element has the type and the storage of the old one, so it replaces the
    // old one transparently, and each reference to the slot reaches it.
    // Value initialization zeroes an element with no user-provided default
    // constructor first, so each padding byte of such an element is zero.
    [[nodiscard]] reference claim_fresh() noexcept
        requires std::is_nothrow_default_constructible_v<T>
    {
        T* const slot = __builtin_addressof(claim());
        slot->~T();
        return *::new(static_cast<void*>(slot)) T();
    }

    constexpr reference push(T const& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
        requires std::is_copy_assignable_v<T>
    {
        reference slot = claim();
        slot = value;
        return slot;
    }

    constexpr reference push(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
        requires std::is_move_assignable_v<T>
    {
        reference slot = claim();
        slot = std::move(value);
        return slot;
    }

    // recent(0) is the slot the last claim wrote.  The result is meaningful
    // only for i < size(): looking back further returns a stale or default
    // slot.  The cursor mask keeps every index inside the storage, so the
    // bound on i is a logic bound and not a safety bound.
    [[nodiscard]] constexpr reference recent(size_type i) noexcept {
        return slot_at_(prove_slot_(cursor_.index_back(i)));
    }
    [[nodiscard]] constexpr const_reference recent(size_type i) const noexcept {
        return storage_.at(prove_slot_(cursor_.index_back(i)));
    }

    // A copy, so no reference into the ring leaves it.  The cursor is one
    // word and trivially copyable, so the copy costs nothing.
    [[nodiscard]] constexpr Cyclic<std::size_t, N> cursor() const noexcept { return cursor_; }
};

}  // namespace fixy
