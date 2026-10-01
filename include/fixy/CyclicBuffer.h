#pragma once

// Bounded ring buffer that keeps the last N elements, evicting the oldest on
// overflow, and reads most recent first.
//
// The fill count is a separate saturating counter rather than
// min(cursor.raw(), N).  The cursor is free-running and wraps at 2^bits, so
// once it laps, `raw < N` becomes true again and a derived size would
// under-report a full ring.

#include <fixy/Cyclic.h>
#include <fixy/FixedArray.h>
#include <fixy/Mutation.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
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
    FixedArray<T, N> storage_{};
    Cyclic<std::size_t, N> cursor_{};
    BoundedMonotonic<std::size_t, N> count_ = mint_bounded_monotonic<std::size_t, N>(std::size_t{0});

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
        reference slot = storage_[cursor_.index()];
        cursor_.advance();
        if (count_.get() < N) count_.bump();
        return slot;
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
    [[nodiscard]] constexpr reference recent(size_type i) noexcept { return storage_[cursor_.index_back(i)]; }
    [[nodiscard]] constexpr const_reference recent(size_type i) const noexcept {
        return storage_[cursor_.index_back(i)];
    }

    // A copy, so no reference into the ring leaves it.  The cursor is one
    // word and trivially copyable, so the copy costs nothing.
    [[nodiscard]] constexpr Cyclic<std::size_t, N> cursor() const noexcept { return cursor_; }
};

}  // namespace fixy
