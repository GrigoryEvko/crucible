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

namespace detail::cyclic_buffer_self_test {

using CB8 = CyclicBuffer<int, 8>;

static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>)
              == sizeof(FixedArray<std::uint32_t, 8>) + sizeof(Cyclic<std::size_t, 8>)
                     + sizeof(BoundedMonotonic<std::size_t, 8>));
static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>) == 48);
static_assert(sizeof(CyclicBuffer<std::uint64_t, 16>) == 144);
static_assert(alignof(CyclicBuffer<std::uint32_t, 8>) == alignof(std::size_t));
static_assert(std::is_trivially_copyable_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(std::is_trivially_destructible_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(!std::is_same_v<CyclicBuffer<int, 8>, FixedArray<int, 8>>);
static_assert(CB8::wrapper_kind() == "structural::CyclicBuffer");

[[nodiscard]] consteval bool default_is_empty() noexcept {
    CB8 b{};
    return b.empty() && !b.full() && b.size() == 0 && CB8::capacity == 8;
}
static_assert(default_is_empty());

[[nodiscard]] consteval bool push_grows_and_recent_tracks() noexcept {
    CB8 b{};
    b.push(10);
    b.push(20);
    b.push(30);
    return b.size() == 3 && b.recent(0) == 30 && b.recent(1) == 20 && b.recent(2) == 10;
}
static_assert(push_grows_and_recent_tracks());

[[nodiscard]] consteval bool claim_then_mutate() noexcept {
    CB8 b{};
    int& slot = b.claim();
    slot = 99;
    return b.size() == 1 && b.recent(0) == 99;
}
static_assert(claim_then_mutate());

[[nodiscard]] consteval bool saturates_and_evicts() noexcept {
    CB8 b{};
    for (int v = 0; v < 12; ++v)
        b.push(v);
    if (b.size() != 8 || !b.full()) return false;
    return b.recent(0) == 11 && b.recent(7) == 4;
}
static_assert(saturates_and_evicts());

[[nodiscard]] consteval bool full_empty_transitions() noexcept {
    CB8 b{};
    if (!b.empty()) return false;
    for (int v = 0; v < 8; ++v)
        b.push(v);
    return b.full() && b.size() == 8;
}
static_assert(full_empty_transitions());

[[nodiscard]] consteval bool cursor_tracks_absolute_count() noexcept {
    CB8 b{};
    for (int v = 0; v < 5; ++v)
        b.push(v);
    return b.cursor().raw() == 5 && b.cursor().index() == 5;
}
static_assert(cursor_tracks_absolute_count());

}  // namespace detail::cyclic_buffer_self_test

}  // namespace fixy
