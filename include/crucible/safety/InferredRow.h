#pragma once

// ── crucible::safety::extract::inferred_row_t ──────────────────────
//
// FOUND-D10 — extract the Met(X) effect row from a function pointer's
// parameter list.  Sibling of FOUND-D11's `inferred_permission_tags_t`
// (which extracts CSL permission tags) — same parameter-walking
// machinery, different classifier:
//
//                D10                              D11
//   inferred_row_t            inferred_permission_tags_t
//   walks parameters,         walks parameters,
//   classifies cap-tag        classifies wrapper-detected
//   types into a Row<Es...>   tags into a perm_set
//
// Mapping from parameter type to Effect atom:
//
//   effects::Alloc  / cap::Alloc   → Effect::Alloc
//   effects::IO     / cap::IO      → Effect::IO
//   effects::Block  / cap::Block   → Effect::Block
//   effects::Bg                    → Effect::Bg
//   effects::Init                  → Effect::Init
//   effects::Test                  → Effect::Test
//   anything else                  → no contribution
//
// The dispatcher consumes `inferred_row_t<&fn>` to compute the
// Subrow-checked obligation a caller's context must satisfy before
// the call lowers — the static side of the cap-token type-system
// fence (CLAUDE.md L0 §safety).
//
// ── Public surface ──────────────────────────────────────────────────
//
//   inferred_row_t<FnPtr>          ::effects::Row<...>; cv-ref
//                                   stripped on each parameter,
//                                   declaration order, NOT
//                                   canonicalized (insert-unique
//                                   semantics — the `Row` type
//                                   is order-sensitive but the
//                                   `Subrow` concept is not).
//   inferred_row_count_v<FnPtr>    Atom count.
//   function_has_effect_v<FnPtr,E> Membership query: does FnPtr's
//                                   row contain Effect E?
//   is_pure_function_v<FnPtr>      Atoms == 0; the row is empty.
//   IsPureFunction<FnPtr>          Concept form of the above.
//
// ── Why insert-unique, not canonicalize ────────────────────────────
//
// EffectRow.h's `Row<Es...>` is structurally order-dependent (the
// METX-2 doc-block calls this out); `Subrow<R1, R2>` is the
// canonical equality relation.  Insert-unique preserves declaration
// order while deduping repeated atoms (a function declared
// `void f(effects::Alloc, effects::Bg)` yields `Row<Alloc, Bg>` —
// the Bg-implied Alloc is NOT auto-expanded; the caller decides
// whether to lift Bg into its constituent atoms via
// `effects::row_union_t<...>` if the dispatcher needs that form).

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

// Classify a single (cv-ref-stripped) parameter type into the Effect
// atom it contributes, if any.  Returns std::type_identity<Acc> when
// the parameter does not carry an effect tag (so the recursion below
// treats no-op as identity on the accumulator row).
template <typename P, typename Acc>
[[nodiscard]] consteval auto accumulate_param_effect() noexcept {
    using namespace ::crucible::effects;
    using PB = std::remove_cvref_t<P>;

    if constexpr (std::is_same_v<PB, cap::Alloc>) {
        return std::type_identity<
            ::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Alloc>>{};
    } else if constexpr (std::is_same_v<PB, cap::IO>) {
        return std::type_identity<
            ::crucible::effects::detail::row_insert_unique_t<Acc, Effect::IO>>{};
    } else if constexpr (std::is_same_v<PB, cap::Block>) {
        return std::type_identity<
            ::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Block>>{};
    } else if constexpr (std::is_same_v<PB, Bg>) {
        return std::type_identity<
            ::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Bg>>{};
    } else if constexpr (std::is_same_v<PB, Init>) {
        return std::type_identity<
            ::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Init>>{};
    } else if constexpr (std::is_same_v<PB, Test>) {
        return std::type_identity<
            ::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Test>>{};
    } else {
        // Parameter does not carry a cap-tag — Acc unchanged.
        return std::type_identity<Acc>{};
    }
}

template <typename P, typename Acc>
using accumulate_param_effect_t =
    typename decltype(accumulate_param_effect<P, Acc>())::type;

// Recursive fold over parameter indices 0..arity-1.  Mirrors D11's
// infer_perm_tags_step pattern.
template <auto FnPtr, std::size_t I, typename Acc, bool AtEnd>
struct infer_row_step;

// Terminator: I has reached arity.  Return the accumulator.
template <auto FnPtr, std::size_t I, typename Acc>
struct infer_row_step<FnPtr, I, Acc, /*AtEnd=*/true> {
    using type = Acc;
};

// Recursive: harvest effect (if any) from param I, recurse to I+1.
template <auto FnPtr, std::size_t I, typename Acc>
struct infer_row_step<FnPtr, I, Acc, /*AtEnd=*/false> {
    using P_I = param_type_t<FnPtr, I>;
    using NextAcc = accumulate_param_effect_t<P_I, Acc>;
    static constexpr std::size_t Next = I + 1;
    using type = typename infer_row_step<
        FnPtr, Next, NextAcc,
        (Next >= arity_v<FnPtr>)>::type;
};

template <auto FnPtr>
using infer_row_raw =
    typename infer_row_step<
        FnPtr, 0,
        ::crucible::effects::EmptyRow,
        (0 >= arity_v<FnPtr>)>::type;

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Inferred effect row from FnPtr's parameter list, in DECLARATION
// ORDER with insert-unique deduplication.  Use `effects::is_subrow_v`
// to compare two signatures' rows declaration-order independently.
template <auto FnPtr>
using inferred_row_t = detail::infer_row_raw<FnPtr>;

// Atom count in the inferred row.
template <auto FnPtr>
inline constexpr std::size_t inferred_row_count_v =
    inferred_row_t<FnPtr>::size;

// Point-query: does FnPtr's parameter list carry an effect of kind E?
// Folds over the inferred row; declaration-order independent.
template <auto FnPtr, ::crucible::effects::Effect E>
inline constexpr bool function_has_effect_v =
    ::crucible::effects::row_contains_v<inferred_row_t<FnPtr>, E>;

// True iff FnPtr's parameter list carries NO cap-tag types — a
// "pure" function in the dispatcher's vocabulary.  Foreground hot-
// path code that holds no context can only call functions where
// this returns true.
template <auto FnPtr>
inline constexpr bool is_pure_function_v =
    inferred_row_count_v<FnPtr> == 0;

template <auto FnPtr>
concept IsPureFunction = is_pure_function_v<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::infer_row_self_test {

inline void f_pure(int, double) noexcept {}
inline void f_alloc(::crucible::effects::Alloc, std::size_t) noexcept {}
inline void f_bg(::crucible::effects::Bg, int) noexcept {}
inline void f_alloc_io(::crucible::effects::Alloc,
                       ::crucible::effects::IO,
                       int) noexcept {}
inline void f_alloc_dup(::crucible::effects::Alloc,
                        ::crucible::effects::Alloc,
                        int) noexcept {}

// ── Pure function — empty row.
static_assert(std::is_same_v<
    inferred_row_t<&f_pure>,
    ::crucible::effects::EmptyRow>);
static_assert(inferred_row_count_v<&f_pure> == 0);
static_assert(is_pure_function_v<&f_pure>);
static_assert(IsPureFunction<&f_pure>);

// ── Single-cap function — singleton row.
static_assert(std::is_same_v<
    inferred_row_t<&f_alloc>,
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>);
static_assert(inferred_row_count_v<&f_alloc> == 1);
static_assert(!is_pure_function_v<&f_alloc>);
static_assert(function_has_effect_v<&f_alloc,
                                    ::crucible::effects::Effect::Alloc>);
static_assert(!function_has_effect_v<&f_alloc,
                                     ::crucible::effects::Effect::IO>);

// ── Bg context — Bg atom (NOT auto-expanded into Alloc/IO/Block).
static_assert(std::is_same_v<
    inferred_row_t<&f_bg>,
    ::crucible::effects::Row<::crucible::effects::Effect::Bg>>);
static_assert(function_has_effect_v<&f_bg,
                                    ::crucible::effects::Effect::Bg>);
static_assert(!function_has_effect_v<&f_bg,
                                     ::crucible::effects::Effect::Alloc>);

// ── Two distinct caps — two-atom row, declaration order.
static_assert(std::is_same_v<
    inferred_row_t<&f_alloc_io>,
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc,
                             ::crucible::effects::Effect::IO>>);
static_assert(inferred_row_count_v<&f_alloc_io> == 2);

// ── Duplicated cap parameter — dedup via insert-unique.
static_assert(std::is_same_v<
    inferred_row_t<&f_alloc_dup>,
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>);
static_assert(inferred_row_count_v<&f_alloc_dup> == 1);

}  // namespace detail::infer_row_self_test

inline bool inferred_row_smoke_test() noexcept {
    using namespace detail::infer_row_self_test;
    using namespace ::crucible::effects;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_pure_function_v<&f_pure>;
        ok = ok && !is_pure_function_v<&f_alloc>;
        ok = ok && (inferred_row_count_v<&f_alloc_io> == 2);
        ok = ok && function_has_effect_v<&f_bg, Effect::Bg>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
