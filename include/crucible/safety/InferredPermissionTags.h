#pragma once

// ── crucible::safety::extract::inferred_permission_tags_t ───────────
//
// FOUND-D11 of 28_04_2026_effects.md §6.1 + 27_04_2026.md §5.5.
// Folds the FOUND-D03 (`is_owned_region_v` + `owned_region_tag_t`)
// and FOUND-D04 (`is_permission_v` / `is_shared_permission_v` +
// `permission_tag_t` / `shared_permission_tag_t`) wrapper-detection
// surfaces over a function's parameter pack to harvest the SET of
// region tags the function operates on.
//
// ── What this header ships ──────────────────────────────────────────
//
//   inferred_permission_tags_raw_t<FnPtr>
//                          The DECLARATION-ORDER PermSet harvested
//                          from the parameter list — deduped via
//                          unique-prepend but NOT canonically sorted.
//                          Used when the dispatcher generates
//                          splits_into_pack in caller-visible order
//                          (e.g., when the user expects the tag list
//                          to mirror their parameter order).
//
//   inferred_permission_tags_t<FnPtr>
//                          A `crucible::safety::proto::PermSet<Tags...>`
//                          deduped + CANONICALIZED (sorted), containing
//                          the region tag of every parameter that is
//                          either an OwnedRegion<T, Tag>, a
//                          Permission<Tag>, or a SharedPermission<Tag>
//                          (after cv-ref stripping).  Parameters
//                          that are not tag-bearing wrappers
//                          contribute nothing.
//
//   function_has_tag_v<FnPtr, Tag>
//                          Point-query: true iff Tag appears in the
//                          inferred set.  Useful for the dispatcher's
//                          per-shape concept gates that ask "does
//                          this function operate on region X?"
//
//   inferred_permission_tags_count_v<FnPtr>
//                          Count of distinct tags in the inferred
//                          set.  Used by the cache-tier rule
//                          (CostModel.h) to pick a parallelism
//                          factor when the function is recognized
//                          as a multi-region transform.
//
//   IsTagFreeFunction<FnPtr>
//                          Concept form: true iff
//                          inferred_permission_tags_t<FnPtr> is
//                          EmptyPermSet (the function takes no
//                          tag-bearing parameters).  Useful for the
//                          dispatcher's pure-shape recognizer
//                          (FOUND-D12).
//
// ── How the dispatcher consumes this ────────────────────────────────
//
// The signature dispatcher (FOUND-D12+) reads
// `inferred_permission_tags_t<FnPtr>` to:
//
// 1. Auto-generate a `splits_into_pack` tree for parallel execution.
//    A function consuming `OwnedRegion<T, A>&&, OwnedRegion<U, B>&&`
//    yields `PermSet<A, B>`; the dispatcher synthesizes
//    `splits_into_pack<Whole, A, B>` and routes through
//    `parallel_for_views<2>`.
//
// 2. Verify the caller has the required permissions.  A function
//    declared `void f(Permission<A>&&, Permission<B>&&)` requires
//    callers to hold both A and B; the dispatcher validates this
//    against the caller's PermSet at the dispatch boundary.
//
// 3. Pre-emptively reject non-canonical functions.  If the harvested
//    PermSet is empty AND the function does not match any other
//    canonical shape, the dispatcher refuses to auto-route and emits
//    a diagnostic naming the unrecognized parameter type.
//
// ── Deduplication discipline ────────────────────────────────────────
//
// Two parameters sharing a tag (e.g., a function consuming two
// slices of the same region as `OwnedRegion<T, A>&&,
// OwnedRegion<U, A>&&`) emit the tag ONCE.  Insertion uses
// `perm_set_insert_t` (the existing unique-prepend from
// `permissions/PermSet.h:149-150`); the final result is
// canonicalized via `perm_set_canonicalize_t` so two functions
// with parameter tags `(A, B)` and `(B, A)` produce the SAME
// PermSet.  This is load-bearing for cache-key stability — the
// federation hash (FOUND-I02) keys on the canonicalized form.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Recursive template fold over parameter indices 0..arity-1, with
// per-parameter conditional tag extraction via `if constexpr` over
// the three wrapper-detection predicates.  The fold terminator uses
// the parameterized AtEnd boolean (computed from `I >= arity_v`)
// because partial specialization on `<FnPtr, arity_v<FnPtr>>` would
// require arity_v to match for every FnPtr — impossible across
// distinct functions.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval projection.
//   TypeSafe — the harvest is a strict union of three predicates'
//              positives.  Non-matching parameters are skipped, not
//              guessed-at; no silent type construction.
//   DetSafe — same FnPtr → same harvested set.  Insertion order is
//              the declaration order; canonicalization removes the
//              dependency on order for downstream hash stability.

#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsPermission.h>

#include <crucible/permissions/PermSet.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: per-parameter tag harvest ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// Per-parameter tag harvest.  Returns std::type_identity<NewAcc>
// where NewAcc is Acc with the parameter's tag inserted (or Acc
// unchanged if the parameter contributes no tag).  Using `if
// constexpr` over the wrapper-detection predicates avoids the
// partial-specialization ambiguity that would arise from
// concept-overloaded specializations (since OwnedRegion and
// Permission are mutually exclusive but the compiler would still
// need to disambiguate the overload set).
template <typename P, typename Acc>
[[nodiscard]] consteval auto accumulate_param_tag() noexcept {
    if constexpr (extract::is_owned_region_v<P>) {
        return std::type_identity<
            ::crucible::safety::proto::perm_set_insert_t<
                Acc, extract::owned_region_tag_t<P>>>{};
    } else if constexpr (extract::is_permission_v<P>) {
        return std::type_identity<
            ::crucible::safety::proto::perm_set_insert_t<
                Acc, extract::permission_tag_t<P>>>{};
    } else if constexpr (extract::is_shared_permission_v<P>) {
        return std::type_identity<
            ::crucible::safety::proto::perm_set_insert_t<
                Acc, extract::shared_permission_tag_t<P>>>{};
    } else {
        // Parameter does not carry a region tag — Acc unchanged.
        return std::type_identity<Acc>{};
    }
}

template <typename P, typename Acc>
using accumulate_param_tag_t =
    typename decltype(accumulate_param_tag<P, Acc>())::type;

// Recursive fold over parameter indices 0..arity-1.  The AtEnd
// parameter is computed externally from arity_v<FnPtr> because
// partial specialization on the arity itself would require it to
// match per FnPtr, which is impossible across distinct functions.

template <auto FnPtr, std::size_t I, typename Acc, bool AtEnd>
struct infer_perm_tags_step;

// Terminator: I has reached arity.  Return the accumulator.
template <auto FnPtr, std::size_t I, typename Acc>
struct infer_perm_tags_step<FnPtr, I, Acc, /*AtEnd=*/true> {
    using type = Acc;
};

// Recursive: harvest tag (if any) from param I, recurse to I+1.
template <auto FnPtr, std::size_t I, typename Acc>
struct infer_perm_tags_step<FnPtr, I, Acc, /*AtEnd=*/false> {
    using P_I = extract::param_type_t<FnPtr, I>;
    using NextAcc = accumulate_param_tag_t<P_I, Acc>;
    static constexpr std::size_t Next = I + 1;
    using type = typename infer_perm_tags_step<
        FnPtr, Next, NextAcc,
        (Next >= extract::arity_v<FnPtr>)>::type;
};

template <auto FnPtr>
using infer_perm_tags_raw =
    typename infer_perm_tags_step<
        FnPtr, 0,
        ::crucible::safety::proto::EmptyPermSet,
        (0 >= extract::arity_v<FnPtr>)>::type;

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Inferred permission tags from the function's parameter list, in
// DECLARATION ORDER.  Deduped via unique-prepend but NOT
// canonically sorted; the dispatcher consumes this when emitting
// splits_into_pack in caller-visible order.
template <auto FnPtr>
using inferred_permission_tags_raw_t =
    detail::infer_perm_tags_raw<FnPtr>;

// Inferred permission tags, CANONICALIZED (sorted unique form) so
// callers can compare two signatures' tag sets without caring about
// declaration order.  Required for federation cache-key stability —
// the row hash (FOUND-I02) keys on the canonical form.
template <auto FnPtr>
using inferred_permission_tags_t =
    ::crucible::safety::proto::perm_set_canonicalize_t<
        inferred_permission_tags_raw_t<FnPtr>>;

// Point-query: does FnPtr's parameter list carry a tag-bearing
// wrapper for THIS specific Tag?  Folds over the canonical form;
// declaration-order independent.  Used by per-shape concept gates
// in FOUND-D12+ that ask "does this function operate on region X?"
template <auto FnPtr, typename Tag>
inline constexpr bool function_has_tag_v =
    ::crucible::safety::proto::perm_set_contains_v<
        inferred_permission_tags_t<FnPtr>, Tag>;

// Count of distinct tags in the inferred set.  Used by the
// cache-tier rule (concurrent/CostModel.h) to set parallelism
// factor when the dispatcher recognizes a multi-region transform —
// each tag corresponds to one parallelizable axis.
template <auto FnPtr>
inline constexpr std::size_t inferred_permission_tags_count_v =
    inferred_permission_tags_t<FnPtr>::size;

// True iff the function takes no tag-bearing parameters.
template <auto FnPtr>
inline constexpr bool is_tag_free_function_v =
    inferred_permission_tags_count_v<FnPtr> == 0;

template <auto FnPtr>
concept IsTagFreeFunction = is_tag_free_function_v<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Negative-side smoke (function with NO tag-bearing parameters
// yields EmptyPermSet) is exercised in-header without leaking
// production wrapper instantiations.  Positive coverage —
// OwnedRegion, Permission, SharedPermission, mixed parameters,
// dedup — lives in the sentinel TU.

namespace detail::infer_perm_tags_self_test {

// Function with no tag-bearing parameters — inferred set is empty.
inline void f_no_tags(int, double, char*) noexcept {}

static_assert(::crucible::safety::proto::perm_set_equal_v<
    inferred_permission_tags_t<&f_no_tags>,
    ::crucible::safety::proto::EmptyPermSet>);

static_assert(is_tag_free_function_v<&f_no_tags>);
static_assert(IsTagFreeFunction<&f_no_tags>);
static_assert(inferred_permission_tags_count_v<&f_no_tags> == 0);

// Nullary function — also empty.
inline void f_nullary() noexcept {}
static_assert(is_tag_free_function_v<&f_nullary>);
static_assert(inferred_permission_tags_count_v<&f_nullary> == 0);

// Raw form on tag-free function is also empty.
static_assert(::crucible::safety::proto::perm_set_equal_v<
    inferred_permission_tags_raw_t<&f_no_tags>,
    ::crucible::safety::proto::EmptyPermSet>);

}  // namespace detail::infer_perm_tags_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool inferred_permission_tags_smoke_test() noexcept {
    using namespace detail::infer_perm_tags_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_tag_free_function_v<&f_no_tags>;
        ok = ok && is_tag_free_function_v<&f_nullary>;
        ok = ok && IsTagFreeFunction<&f_no_tags>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
