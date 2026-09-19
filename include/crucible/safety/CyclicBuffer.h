#pragma once

// Bounded ring buffer that keeps the last N elements, evicting the
// oldest on overflow, and reads most-recent first.
//
// The fill count is a separate saturating counter rather than
// min(cursor.raw(), N).  The cursor is free-running and wraps at
// 2^bits, so once it laps, `raw < N` becomes true again and a derived
// size would under-report a full ring.

#include <crucible/Platform.h>
#include <crucible/safety/Cyclic.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/_Mutation.h>

#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

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
    BoundedMonotonic<std::size_t, N> count_{std::size_t{0}};

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

    // The returned slot still holds its prior value.  The caller
    // overwrites it.  The reference stays valid until N further claims
    // wrap back onto the same slot.
    //
    // The order is bind-at-current, then advance.  advance() touches
    // only the cursor, so the reference into the storage survives it.
    //
    // The fill guard is load-bearing, not defensive.  bump() does not
    // saturate: it carries a precondition that the count is below the
    // bound.  Calling it on a full ring aborts under enforced
    // contracts, and is undefined behaviour once the precondition
    // lowers to an assumption.
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

    // recent(0) is the slot the last claim wrote.  The result is
    // meaningful only for i < size(); looking back further returns a
    // stale or default slot.  There is no precondition: the cursor mask
    // keeps every index inside the storage, so the bound on i is a
    // logic bound and not a safety bound.
    [[nodiscard]] constexpr reference recent(size_type i) noexcept { return storage_[cursor_.index_back(i)]; }
    [[nodiscard]] constexpr const_reference recent(size_type i) const noexcept {
        return storage_[cursor_.index_back(i)];
    }

    [[nodiscard]] constexpr Cyclic<std::size_t, N> const& cursor() const noexcept { return cursor_; }
};

static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>)
              == sizeof(FixedArray<std::uint32_t, 8>) + sizeof(Cyclic<std::size_t, 8>)
                     + sizeof(BoundedMonotonic<std::size_t, 8>));
static_assert(sizeof(CyclicBuffer<std::uint64_t, 16>)
              == sizeof(FixedArray<std::uint64_t, 16>) + sizeof(Cyclic<std::size_t, 16>)
                     + sizeof(BoundedMonotonic<std::size_t, 16>));

static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>) == 48);
static_assert(sizeof(CyclicBuffer<std::uint64_t, 16>) == 144);

static_assert(alignof(CyclicBuffer<std::uint32_t, 8>) == alignof(std::size_t));

static_assert(std::is_trivially_copyable_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(std::is_trivially_destructible_v<CyclicBuffer<std::uint32_t, 8>>);

static_assert(!std::is_same_v<CyclicBuffer<int, 8>, FixedArray<int, 8>>);

namespace detail::cyclic_buffer_self_test {

using CB8 = CyclicBuffer<int, 8>;

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

static_assert(CB8::wrapper_kind() == "structural::CyclicBuffer");

inline void runtime_smoke_test() {
    CB8 b{};
    if (!b.empty() || b.size() != 0) std::abort();

    volatile int seed = 100;
    for (int k = 0; k < 3; ++k)
        b.push(static_cast<int>(seed) + k);
    if (b.size() != 3) std::abort();
    if (b.recent(0) != 102 || b.recent(2) != 100) std::abort();

    int& slot = b.claim();
    slot = 555;
    if (b.size() != 4 || b.recent(0) != 555) std::abort();

    CB8 r{};
    for (int v = 0; v < 20; ++v)
        r.push(static_cast<int>(seed) + v);
    if (r.size() != 8 || !r.full()) std::abort();
    if (r.recent(0) != 119) std::abort();
    if (r.recent(7) != 112) std::abort();

    if (r.cursor().raw() != 20) std::abort();
    if (r.cursor().index() != (20u & 7u)) std::abort();
}

}  // namespace detail::cyclic_buffer_self_test

}  // namespace crucible::safety
