#pragma once

// ── crucible::safety::Linear<T> ─────────────────────────────────────
//
// Move-only wrapper enforcing "consumed exactly once" semantics.
//
//   Axiom coverage: MemSafe, LeakSafe, BorrowSafe (code_guide §II).
//   Runtime cost:   zero.  sizeof(Linear<T>) == sizeof(T).
//
// - Copy is deleted with a reason string.
// - Move is defaulted; moved-from state is left to T's move semantics.
// - [[nodiscard]] on the class forces capture at construction sites.
// - .consume() && returns the inner T (compile error on lvalue).
// - .peek() const& / .peek_mut() & borrow without consuming.
// - drop(std::move(x)) explicit discard.
// - In-place construction via std::in_place_t avoids a move.
//
// Paired with -Werror=use-after-move, double-consume = compile error.
//
// Pattern: wrap every resource-carrying type (file handle, mmap region,
// arena-owned object with drop semantics, channel endpoint).

#include <crucible/Platform.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T>
class [[nodiscard]] Linear {
    T value_;

public:
    using value_type = T;

    // Move-from-T construction.
    constexpr explicit Linear(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)} {}

    // In-place construction: build T directly inside the Linear.  Avoids a
    // move when T is expensive to move or non-movable by convention.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Linear(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : value_{std::forward<Args>(args)...} {}

    Linear(const Linear&)            = delete("Linear<T> is move-only; use std::move or drop()");
    Linear& operator=(const Linear&) = delete("Linear<T> is move-only; use std::move or drop()");
    Linear(Linear&&)                 = default;
    Linear& operator=(Linear&&)      = default;
    ~Linear()                        = default;

    // Ownership transfer — must be called on rvalue.  Compile error on lvalue.
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_);
    }

    // Shared borrow — Linear keeps the value.
    [[nodiscard]] constexpr const T& peek() const & noexcept { return value_; }

    // Mutable borrow — Linear keeps the value but caller may mutate.
    // Prefer consume+reconstruct over this when the change is semantic.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return value_; }

    // Swap — preserves linearity on both sides.
    constexpr void swap(Linear& other) noexcept(std::is_nothrow_swappable_v<T>) {
        using std::swap;
        swap(value_, other.value_);
    }

    friend constexpr void swap(Linear& a, Linear& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }
};

template <typename T>
Linear(T) -> Linear<T>;

// Factory: construct a Linear<T> by forwarding args to T's constructor.
// Equivalent to Linear<T>{std::in_place, args...} but deduces T less ambiguously.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> make_linear(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return Linear<T>{std::in_place, std::forward<Args>(args)...};
}

// Explicit discard — equivalent to `let _ = std::move(x).consume();`
template <typename T>
constexpr void drop(Linear<T>&& x)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    (void)std::move(x).consume();
}

// Zero-cost guarantee.
static_assert(sizeof(Linear<int>)        == sizeof(int));
static_assert(sizeof(Linear<void*>)      == sizeof(void*));
static_assert(sizeof(Linear<long long>)  == sizeof(long long));

} // namespace crucible::safety
