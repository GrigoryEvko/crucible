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
//
// ── MIGRATED to Graded<Absolute, QttSemiring::At<One>, T>  (#461) ──
//
// As of MIGRATE-1 (2026-04-26) Linear<T> is a thin wrapper around the
// algebraic primitive
//
//   Graded<ModalityKind::Absolute,
//          QttSemiring::At<QttGrade::One>,
//          T>
//
// per misc/25_04_2026.md §2.3.  The wrapper preserves every existing
// public API surface (consume / peek / peek_mut / swap / make_linear /
// drop / in_place ctor / [[nodiscard]] / move-only deletion).  Storage
// is delegated to Graded; the lattice's element_type is empty
// (singleton "linearity grade One") and EBO collapses both grade_ and
// the wrapper itself, so sizeof(Linear<T>) == sizeof(T) is preserved
// by structural guarantee.
//
// QTT (Atkey 2018, Brady 2021) = quantitative-type-theory linearity
// semiring {0, 1, ω}, +, ·.  Grade 0 = "discarded", Grade 1 = "exactly
// once" (Linear<T>'s discipline), Grade ω = "unrestricted" (standard
// C++ value semantics).  Linear<T> instantiates only the grade-1
// case; future Affine<T> / Unrestricted<T> wrappers (if needed) would
// alias the other two grades over the same Graded substrate.
//
// peek_mut and swap forward to Graded::peek_mut and Graded::swap,
// which are gated on AbsoluteModality<M> for soundness — the lattice
// position is a static property of the wrapper, not derived from the
// inner T's bytes, so raw mutation cannot violate the grade.  See
// Graded.h's peek_mut/swap doc-block for the full rationale.
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T>
class [[nodiscard]] Linear {
    // The QTT grade-1 lattice (singleton "exactly one ownership").
    using lattice_type = ::crucible::algebra::lattices::QttSemiring::At<
        ::crucible::algebra::lattices::QttGrade::One>;

    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

    // Empty-lattice grade_type collapses via [[no_unique_address]] in
    // Graded; impl_ is sizeof(T).  Wrapper adds no other state.
    graded_type impl_;

    // Helper: the singleton element_type value.  QttSemiring::At<One>
    // has an empty element_type, so this is a zero-cost {} construction.
    [[nodiscard]] static constexpr typename lattice_type::element_type
    grade_one() noexcept {
        return typename lattice_type::element_type{};
    }

public:
    using value_type = T;

    // Move-from-T construction.  Forwards to Graded(value, grade).
    constexpr explicit Linear(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), grade_one()} {}

    // In-place construction: build T directly inside the Linear.
    // Avoids a move when T is expensive to move or non-movable by
    // convention.  The Args... are forwarded to T's constructor; the
    // resulting T is then moved into Graded — for move-elidable
    // configurations the move collapses, for non-movable T the
    // construction happens in-situ via guaranteed copy elision.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Linear(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), grade_one()} {}

    Linear(const Linear&)            = delete("Linear<T> is move-only; use std::move or drop()");
    Linear& operator=(const Linear&) = delete("Linear<T> is move-only; use std::move or drop()");
    Linear(Linear&&)                 = default;
    Linear& operator=(Linear&&)      = default;
    ~Linear()                        = default;

    // Ownership transfer — must be called on rvalue.  Compile error
    // on lvalue.  Forwards to Graded::consume().
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // Shared borrow — Linear keeps the value.  Forwards to
    // Graded::peek().
    [[nodiscard]] constexpr const T& peek() const & noexcept {
        return impl_.peek();
    }

    // Mutable borrow — Linear keeps the value but caller may mutate.
    // Prefer consume+reconstruct over this when the change is
    // semantic.  Forwards to Graded::peek_mut(), gated on
    // AbsoluteModality<M> in Graded — the QTT-At-One grade is a
    // static property of the wrapper (linearity is about ownership,
    // not value identity), so raw mutation is sound.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // Swap — preserves linearity on both sides.  Forwards to
    // Graded::swap (also gated on AbsoluteModality).
    constexpr void swap(Linear& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Linear& a, Linear& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }
};

template <typename T>
Linear(T) -> Linear<T>;

// Factory: construct a Linear<T> by forwarding args to T's constructor.
// Equivalent to Linear<T>{std::in_place, args...} but deduces T less
// ambiguously.
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

// Zero-cost guarantee — preserved through the Graded delegation by
// the lattice's empty element_type + Graded's [[no_unique_address]]
// on grade_.  If this fires after a Graded refactor, the EBO
// discipline regressed.
static_assert(sizeof(Linear<int>)        == sizeof(int));
static_assert(sizeof(Linear<void*>)      == sizeof(void*));
static_assert(sizeof(Linear<long long>)  == sizeof(long long));

} // namespace crucible::safety
