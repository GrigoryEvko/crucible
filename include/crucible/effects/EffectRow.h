#pragma once

// ── crucible::effects::Row — Met(X) effect-row algebra ──────────────
//
// A Row is a compile-time set of Effect atoms.  Rows compose by set
// union; Subrow inclusion is the substitution principle for capability
// propagation (a function requiring row R can be called from a context
// holding any row R' ⊇ R).
//
// Per 25_04_2026.md §3.2 and Tang-Lindley POPL 2026, the row is
// modelled as a `consteval std::meta::info` set.  This header declares
// the public surface; bodies land in METX-2 (#474).
//
//   Axiom coverage: TypeSafe — Row is strongly typed; mismatches at
//                   capability-propagation boundaries fire at template
//                   substitution, not at use site.
//                   DetSafe — every row operation is consteval.
//   Runtime cost:   zero.  Rows have no runtime representation; the
//                   set lives purely in the type system / consteval
//                   evaluation.
//
// Public surface:
//   Row<Es...>            — concrete row type carrying the effect pack
//   row_size_v<R>         — atom count
//   row_contains_v<R, E>  — membership predicate
//   Subrow<R₁, R₂>        — concept: R₁ ⊆ R₂
//   row_union_t<R₁, R₂>   — set union (canonicalized)
//   row_difference_t<R₁, R₂> — R₁ \ R₂
//   row_intersection_t<R₁, R₂> — R₁ ∩ R₂
//
// STATUS: stub.  Class type is COMPLETE so that Computation<Row, T>
// and the METX-4 compat shim can name it.  Set operations land in
// METX-2 (#474); calling them before that ships fails to compile with
// the implementation-task pointer.
//
// See Capabilities.h (atoms), Computation.h (carrier), compat/Fx.h
// (backward-compat aliases).

#include <crucible/effects/Capabilities.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace crucible::effects {

// ── Row<Es...> ──────────────────────────────────────────────────────
//
// Carries a pack of Effect atoms.  No runtime state.  All algebra is
// consteval-only via the trait family below.
//
// Canonicalization (sort + dedup) is METX-2's job — until then,
// `Row<Effect::Bg, Effect::Alloc>` and `Row<Effect::Alloc, Effect::Bg>`
// are *distinct types*.  Code that doesn't depend on canonical
// equality compiles fine; code that does will need to wait for #474.
template <Effect... Es>
struct Row {
    static constexpr std::size_t size = sizeof...(Es);
};

using EmptyRow = Row<>;

// ── Membership / size traits ────────────────────────────────────────

template <typename R>
inline constexpr std::size_t row_size_v = R::size;

// Membership predicate: does row R contain effect atom E?
//
// Implemented now (small enough that deferring would be silly).
template <typename R, Effect E>
inline constexpr bool row_contains_v = false;

template <Effect E, Effect... Es>
inline constexpr bool row_contains_v<Row<Es...>, E> = ((Es == E) || ...);

// ── Set algebra (bodies land in METX-2 #474) ────────────────────────
//
// row_union_t / row_difference_t / row_intersection_t are declared but
// defined as deleted aliases via a sentinel `void` — any use before
// METX-2 fires a compile error pointing at the implementation task.

namespace detail {
    template <typename R1, typename R2>
    struct deferred_row_union {
        // Use of this trait fires the static_assert ONLY at
        // instantiation site (template-dependent), not on header read.
        static_assert(sizeof(R1*) == 0,
            "row_union_t: implementation deferred to METX-2 (#474)");
        using type = void;
    };

    template <typename R1, typename R2>
    struct deferred_row_difference {
        static_assert(sizeof(R1*) == 0,
            "row_difference_t: implementation deferred to METX-2 (#474)");
        using type = void;
    };

    template <typename R1, typename R2>
    struct deferred_row_intersection {
        static_assert(sizeof(R1*) == 0,
            "row_intersection_t: implementation deferred to METX-2 (#474)");
        using type = void;
    };
}  // namespace detail

template <typename R1, typename R2>
using row_union_t = typename detail::deferred_row_union<R1, R2>::type;

template <typename R1, typename R2>
using row_difference_t = typename detail::deferred_row_difference<R1, R2>::type;

template <typename R1, typename R2>
using row_intersection_t = typename detail::deferred_row_intersection<R1, R2>::type;

// ── Subrow concept ──────────────────────────────────────────────────
//
// R1 is a Subrow of R2 iff every atom in R1 is also in R2.
// Implemented now (the substitution principle is the ONE check
// that pervades every capability-propagation call site, so deferring
// it would block METX-5's sweep before it starts).
template <typename R1, typename R2>
struct is_subrow : std::false_type {};

template <Effect... E1s, Effect... E2s>
struct is_subrow<Row<E1s...>, Row<E2s...>>
    : std::bool_constant<(row_contains_v<Row<E2s...>, E1s> && ...)> {};

template <typename R1, typename R2>
inline constexpr bool is_subrow_v = is_subrow<R1, R2>::value;

template <typename R1, typename R2>
concept Subrow = is_subrow_v<R1, R2>;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::effect_row_self_test {

using R_empty       = Row<>;
using R_alloc       = Row<Effect::Alloc>;
using R_io          = Row<Effect::IO>;
using R_alloc_io    = Row<Effect::Alloc, Effect::IO>;
using R_alloc_io_bg = Row<Effect::Alloc, Effect::IO, Effect::Bg>;

// Sizes match the pack.
static_assert(row_size_v<R_empty>       == 0);
static_assert(row_size_v<R_alloc>       == 1);
static_assert(row_size_v<R_alloc_io>    == 2);
static_assert(row_size_v<R_alloc_io_bg> == 3);

// Membership.
static_assert(!row_contains_v<R_empty,    Effect::Alloc>);
static_assert( row_contains_v<R_alloc,    Effect::Alloc>);
static_assert(!row_contains_v<R_alloc,    Effect::IO>);
static_assert( row_contains_v<R_alloc_io, Effect::Alloc>);
static_assert( row_contains_v<R_alloc_io, Effect::IO>);
static_assert(!row_contains_v<R_alloc_io, Effect::Bg>);

// Subrow inclusion (substitution principle).
static_assert( is_subrow_v<R_empty, R_empty>);
static_assert( is_subrow_v<R_empty, R_alloc>);          // ∅ ⊆ {Alloc}
static_assert( is_subrow_v<R_alloc, R_alloc_io>);        // {A} ⊆ {A, I}
static_assert(!is_subrow_v<R_alloc_io, R_alloc>);        // {A, I} ⊄ {A}
static_assert( is_subrow_v<R_alloc_io, R_alloc_io_bg>);  // {A, I} ⊆ {A, I, B}
static_assert(!is_subrow_v<R_io, R_alloc>);              // {I} ⊄ {A}

// Concept-form mirror.
static_assert( Subrow<R_empty, R_alloc_io>);
static_assert( Subrow<R_alloc, R_alloc_io_bg>);
static_assert(!Subrow<R_alloc_io, R_alloc>);

}  // namespace detail::effect_row_self_test

}  // namespace crucible::effects
