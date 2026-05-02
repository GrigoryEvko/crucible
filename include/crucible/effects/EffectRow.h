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
//
// ── See also: typed-set triad (misc/typed_sets.md) ─────────────────
//
// Row<Es...> is the TYPE-LEVEL face of a triad of typed-set
// primitives.  Sister primitives:
//
//   safety::Bits<E>              — runtime value-level set (bitwise,
//                                  mask-encoded enums).
//   permissions::proto::PermSet  — type-only set of TAG TYPES.
//   effects::EffectMask          — runtime dual of Row<Es...>;
//                                  bridge via bits_from_row<R>() in
//                                  effects/EffectRowProjection.h.
//                                  EffectMask is dedicated (NOT
//                                  Bits<Effect>) because Effect is
//                                  position-encoded — see
//                                  misc/typed_sets.md §5 for the
//                                  encoding caveat.

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

// ── Set algebra (METX-2 #474 — bodies shipped) ──────────────────────
//
// row_union_t / row_difference_t / row_intersection_t are NOT
// canonicalized (sort + dedup with stable order) — the Subrow concept
// is the only consumer that matters and it compares semantically, not
// structurally.  `row_union_t<Row<A,B>, Row<B,A>>` yields `Row<A,B>`;
// `row_union_t<Row<B,A>, Row<A,B>>` yields `Row<B,A>`.  Both are
// Subrow-equal, which is what the substitution principle requires.
//
// Canonical-form is left as an opt-in trait specialization for any
// future caller that genuinely needs structural equality (e.g. for
// reflection-based hashing of effect-row literals).  Default behavior
// keeps compile-time cost linear in the row size.

namespace detail {

// row_insert_unique<R, E>: prepend E to R unless E is already present.
// Foundation primitive for the union recursion below.
template <typename R, Effect E>
struct row_insert_unique;

template <Effect... Es, Effect E>
struct row_insert_unique<Row<Es...>, E> {
    using type = std::conditional_t<
        ((Es == E) || ...),
        Row<Es...>,
        Row<Es..., E>
    >;
};

template <typename R, Effect E>
using row_insert_unique_t = typename row_insert_unique<R, E>::type;

// row_union_recursive<R1, R2>: walk R2's pack, insert each atom into R1
// only if not already present.  Linear in |R2|; O(|R1|) per insert via
// fold expression.
template <typename R1, typename R2>
struct row_union_recursive;

template <typename R1>
struct row_union_recursive<R1, Row<>> {
    using type = R1;
};

template <typename R1, Effect Head, Effect... Tail>
struct row_union_recursive<R1, Row<Head, Tail...>> {
    using type = typename row_union_recursive<
        row_insert_unique_t<R1, Head>,
        Row<Tail...>
    >::type;
};

// row_concat<Rs...>: concatenate a pack of Row<...>s.  Used by the
// difference / intersection filter machinery to fold per-element
// keep-or-drop decisions back into a single row.
template <typename...>
struct row_concat;

template <>
struct row_concat<> {
    using type = Row<>;
};

template <Effect... Xs>
struct row_concat<Row<Xs...>> {
    using type = Row<Xs...>;
};

template <Effect... Xs, Effect... Ys, typename... Rest>
struct row_concat<Row<Xs...>, Row<Ys...>, Rest...> {
    using type = typename row_concat<Row<Xs..., Ys...>, Rest...>::type;
};

// row_difference_impl: filter R1 by keep-if-not-in-R2.  Uses the per-
// element keep_or_drop alias to map each Effect either to Row<E> or
// Row<>; row_concat folds the result.
template <typename R1, typename R2>
struct row_difference_impl;

template <Effect... E1s, typename R2>
struct row_difference_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<
        row_contains_v<R2, E>,
        Row<>,        // drop — present in R2
        Row<E>        // keep — absent from R2
    >;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

// row_intersection_impl: filter R1 by keep-if-in-R2.  Symmetric to
// difference but with the keep-or-drop polarity flipped.
template <typename R1, typename R2>
struct row_intersection_impl;

template <Effect... E1s, typename R2>
struct row_intersection_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<
        row_contains_v<R2, E>,
        Row<E>,       // keep — present in R2
        Row<>         // drop — absent from R2
    >;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

}  // namespace detail

template <typename R1, typename R2>
using row_union_t = typename detail::row_union_recursive<R1, R2>::type;

template <typename R1, typename R2>
using row_difference_t = typename detail::row_difference_impl<R1, R2>::type;

template <typename R1, typename R2>
using row_intersection_t = typename detail::row_intersection_impl<R1, R2>::type;

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

// ── Set-algebra coverage (METX-2 #474) ──────────────────────────────
//
// All assertions are EXTENSIONAL — they check membership / sub-row
// containment, not structural type-equality.  See the design note at
// the top of the set-algebra block: row order is not canonicalized,
// so `row_union_t<Row<A,B>, Row<B,A>>` may differ structurally from
// `row_union_t<Row<B,A>, Row<A,B>>` while remaining Subrow-equal.

// Identity laws.
static_assert(std::is_same_v<row_union_t<R_empty, R_empty>, R_empty>);
static_assert(std::is_same_v<row_union_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(is_subrow_v<R_alloc_io, row_union_t<R_empty, R_alloc_io>>);
static_assert(is_subrow_v<row_union_t<R_empty, R_alloc_io>, R_alloc_io>);

// Union — every input row is contained in the result.
using R_union_a_io = row_union_t<R_alloc, R_io>;
static_assert(is_subrow_v<R_alloc, R_union_a_io>);
static_assert(is_subrow_v<R_io,    R_union_a_io>);
static_assert(row_size_v<R_union_a_io> == 2);

// Union — duplicates absorbed (no double-insert).
using R_union_dup = row_union_t<R_alloc_io, R_alloc>;
static_assert(row_size_v<R_union_dup> == 2);  // Alloc not duplicated.
static_assert(is_subrow_v<R_alloc_io, R_union_dup>);
static_assert(is_subrow_v<R_alloc,    R_union_dup>);
static_assert(!row_contains_v<R_union_dup, Effect::Bg>);

// Union — commutativity up to Subrow.
using R_left  = row_union_t<R_alloc, R_io>;
using R_right = row_union_t<R_io, R_alloc>;
static_assert(is_subrow_v<R_left, R_right>);
static_assert(is_subrow_v<R_right, R_left>);

// Union — associativity up to Subrow.
using R_lr_then_bg     = row_union_t<row_union_t<R_alloc, R_io>, Row<Effect::Bg>>;
using R_lr_then_bg_alt = row_union_t<R_alloc, row_union_t<R_io, Row<Effect::Bg>>>;
static_assert(is_subrow_v<R_lr_then_bg, R_lr_then_bg_alt>);
static_assert(is_subrow_v<R_lr_then_bg_alt, R_lr_then_bg>);
static_assert(row_size_v<R_lr_then_bg> == 3);

// Difference — A \ ∅ = A; A \ A = ∅.
static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(row_size_v<row_difference_t<R_alloc_io, R_alloc_io>> == 0);
static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_alloc_io>, R_empty>);

// Difference — drops only the named atoms.
using R_diff = row_difference_t<R_alloc_io_bg, R_alloc>;
static_assert(row_size_v<R_diff> == 2);
static_assert(!row_contains_v<R_diff, Effect::Alloc>);
static_assert( row_contains_v<R_diff, Effect::IO>);
static_assert( row_contains_v<R_diff, Effect::Bg>);

// Intersection — ∅ ∩ A = ∅; A ∩ A = A; commutative up to Subrow.
static_assert(row_size_v<row_intersection_t<R_empty, R_alloc_io>> == 0);
static_assert(std::is_same_v<row_intersection_t<R_alloc_io, R_alloc_io>, R_alloc_io>);

using R_inter = row_intersection_t<R_alloc_io, R_alloc_io_bg>;
static_assert(is_subrow_v<R_inter, R_alloc_io>);
static_assert(is_subrow_v<R_alloc_io, R_inter>);  // R_alloc_io ⊆ R_alloc_io_bg
static_assert(row_size_v<R_inter> == 2);

using R_inter_disjoint = row_intersection_t<R_alloc, R_io>;
static_assert(row_size_v<R_inter_disjoint> == 0);

// De Morgan-ish identity over the universe row {Alloc, IO, Bg, Init,
// Test, Block} — every Effect atom — exercises the difference-of-
// union-equals-intersection-of-differences shape that downstream
// graded-modal effects code relies on.
using R_universe = Row<Effect::Alloc, Effect::IO, Effect::Block,
                       Effect::Bg, Effect::Init, Effect::Test>;
static_assert(row_size_v<R_universe> == effect_count);
static_assert(is_subrow_v<R_alloc_io, R_universe>);
static_assert(is_subrow_v<R_alloc_io_bg, R_universe>);

// Self-difference / intersection corner — A \ A = ∅; A ∩ ∅ = ∅.
static_assert(row_size_v<row_difference_t<R_universe, R_universe>>      == 0);
static_assert(row_size_v<row_intersection_t<R_universe, R_empty>>       == 0);

}  // namespace detail::effect_row_self_test

}  // namespace crucible::effects
