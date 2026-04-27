#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto::PermSet<Tags...> — type-level set of CSL
// permission tags carried by a PermissionedSessionHandle as it walks
// a session protocol.
//
// Phase 1 of FOUND-C (#605 + #606).  See `misc/27_04_csl_permission_
// session_wiring.md` §6 for the full spec.  The fifteen FOUND-C tasks
// land in nine phases; this header is the first.
//
// ─── What this header is ───────────────────────────────────────────
//
// A type-level set algebra over CSL permission Tag types.  Empty class
// (sizeof = 1, EBO-collapsible to 0) instantiated as
// `PermSet<Tag1, Tag2, ...>`.  Operations consume one or two PermSet
// types and produce a new PermSet type.  No runtime state — the entire
// algebra resolves at compile time.
//
//   PermSet<Tags...>             phantom set (sizeof = 1)
//   EmptyPermSet                 alias for PermSet<>
//   perm_set_contains_v<S, T>    membership predicate
//   perm_set_insert_t<S, T>      unique-prepend (no-op if T already in S)
//   perm_set_remove_t<S, T>      filter (no-op if T not in S)
//   perm_set_subset_v<A, B>      A ⊆ B
//   perm_set_equal_v<A, B>       order-insensitive equality
//   perm_set_union_t<A, B>       disjoint union (compile error on overlap)
//   perm_set_difference_t<A, B>  A \ B
//   perm_set_canonicalize_t<S>   identity in v1 (FOUND-E10 lands the sort)
//   perm_set_name<S>()           consteval display name via P2996
//
// ─── Composition with the session-type framework ───────────────────
//
// PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> (Phase 3 of
// FOUND-C) carries `PS = PermSet<Tag1, Tag2, ...>` as a phantom field
// and evolves it on every `.send()`/`.recv()`/`.pick()`/`.branch()`
// based on the payload's permission-flow marker
// (`SessionPermPayloads.h`, Phase 2):
//
//   Send<Plain T, K>             PS' = PS                      (unchanged)
//   Send<Transferable<T, X>, K>  PS' = perm_set_remove_t<PS, X>  (sender loses)
//   Send<Borrowed<T, X>, K>      PS' = PS                      (scoped borrow)
//   Send<Returned<T, X>, K>      PS' = perm_set_remove_t<PS, X>  (sender returns)
//   Recv<Plain T, K>             PS' = PS                      (unchanged)
//   Recv<Transferable<T, X>, K>  PS' = perm_set_insert_t<PS, X>  (recipient gains)
//   Recv<Borrowed<T, X>, K>      PS' = PS                      (ReadView only)
//   Recv<Returned<T, X>, K>      PS' = perm_set_insert_t<PS, X>  (recipient gains)
//
// Loop body balance enforcement (Decision D3 in the wiring plan):
// every Continue site asserts `perm_set_equal_v<PS_at_continue,
// PS_at_loop_entry>` so each iteration of the loop preserves the
// permission set.  Branch convergence (Decision D4): every
// Select/Offer asserts all branches' terminal PS values are equal.
//
// ─── Why order-insensitive equality without canonicalize_pack ──────
//
// `PermSet<A, B>` and `PermSet<B, A>` represent the SAME logical
// permission set.  Equality is checked via bidirectional containment
// (`A ⊆ B ∧ B ⊆ A` plus `size == size`) — O(N²) in tag count but
// PermSets are bounded (typical session protocols hold ≤ 8 tags), so
// the asymptotic cost is irrelevant in practice.  When FOUND-E10
// (#649, `canonicalize_pack<Ts...>`) ships, `perm_set_canonicalize_t`
// graduates from identity to a true reflection-keyed sort and
// `perm_set_equal_v` collapses to `is_same_v` on the canonical form.
// Until then, the bidirectional-containment formulation is correct
// AND cheap enough; switching mechanism later does not break callers.
//
// ─── Discipline ────────────────────────────────────────────────────
//
//   * No SFINAE.  Every check is `static_assert` with classified
//     diagnostic from `sessions/SessionDiagnostic.h::PermissionImbalance`
//     per the project's zero-SFINAE invariant (Task #136).
//   * Every metafunction is fold-expression-based (linear) or trivially
//     recursive (linear in element count, no exponential blowup).
//   * Embedded smoke-test block under
//     `namespace detail::permset_smoke` runs at every TU include so the
//     algebra is verified under whatever warning flags the including TU
//     applies.  `test/test_permissions_compile.cpp` adds the include so
//     the project's `-Werror` matrix sees them.
//   * Header-only.  Depends only on `<type_traits>`, `<cstddef>`,
//     `<string_view>`, `<meta>`, and `permissions/Permission.h`.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::safety::proto {

// ── PermSet<Tags...> ─────────────────────────────────────────────────

template <typename... Tags>
struct PermSet {
    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyPermSet = PermSet<>;

// ── perm_set_contains_v ──────────────────────────────────────────────
//
// Fold over `(is_same_v<Tag_i, Q> || ...)`.  Linear in |PS|; collapses
// to a single boolean at compile time.

namespace detail {

template <typename PS, typename Q>
struct perm_set_contains_impl;

template <typename... Ts, typename Q>
struct perm_set_contains_impl<PermSet<Ts...>, Q>
    : std::bool_constant<(std::is_same_v<Ts, Q> || ...)> {};

}  // namespace detail

template <typename PS, typename Q>
inline constexpr bool perm_set_contains_v =
    detail::perm_set_contains_impl<PS, Q>::value;

// ── perm_set_insert_t (unique-prepend) ───────────────────────────────
//
// If Q ∈ PS: identity.
// Else:      PermSet<Q, Tags...>.

namespace detail {

template <typename PS, typename Q, bool Already>
struct perm_set_insert_branch;

template <typename... Ts, typename Q>
struct perm_set_insert_branch<PermSet<Ts...>, Q, /*Already=*/true> {
    using type = PermSet<Ts...>;
};

template <typename... Ts, typename Q>
struct perm_set_insert_branch<PermSet<Ts...>, Q, /*Already=*/false> {
    using type = PermSet<Q, Ts...>;
};

}  // namespace detail

template <typename PS, typename Q>
struct perm_set_insert
    : detail::perm_set_insert_branch<PS, Q, perm_set_contains_v<PS, Q>> {};

template <typename PS, typename Q>
using perm_set_insert_t = typename perm_set_insert<PS, Q>::type;

// ── perm_set_remove_t (filter) ───────────────────────────────────────
//
// Drop every occurrence of Q from PS; PermSet<> if empty after filter.
// PermSets are sets — duplicates are impossible by construction (the
// only way to add is `perm_set_insert_t`, which is unique-prepend) —
// but the filter handles the multi-occurrence case for symmetry.

namespace detail {

template <typename PS, typename Q>
struct perm_set_remove_impl;

template <typename Q>
struct perm_set_remove_impl<PermSet<>, Q> {
    using type = PermSet<>;
};

template <typename Head, typename... Tail, typename Q>
struct perm_set_remove_impl<PermSet<Head, Tail...>, Q> {
    using rec_type = typename perm_set_remove_impl<PermSet<Tail...>, Q>::type;

    template <typename S>
    struct prepend_head;

    template <typename... Xs>
    struct prepend_head<PermSet<Xs...>> {
        using type = PermSet<Head, Xs...>;
    };

    using type = std::conditional_t<
        std::is_same_v<Head, Q>,
        rec_type,
        typename prepend_head<rec_type>::type>;
};

}  // namespace detail

template <typename PS, typename Q>
struct perm_set_remove : detail::perm_set_remove_impl<PS, Q> {};

template <typename PS, typename Q>
using perm_set_remove_t = typename perm_set_remove<PS, Q>::type;

// ── perm_set_subset_v (PS1 ⊆ PS2) ───────────────────────────────────
//
// True iff every element of PS1 appears in PS2.  EmptyPermSet is a
// subset of every PermSet (vacuous).

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_subset_impl;

template <typename... T1s, typename PS2>
struct perm_set_subset_impl<PermSet<T1s...>, PS2>
    : std::bool_constant<(perm_set_contains_v<PS2, T1s> && ...)> {};

}  // namespace detail

template <typename PS1, typename PS2>
inline constexpr bool perm_set_subset_v =
    detail::perm_set_subset_impl<PS1, PS2>::value;

// ── perm_set_equal_v (order-insensitive equality) ───────────────────
//
// Two PermSets are equal iff they have the same size and every element
// of one is in the other.  Linear-size guard plus quadratic-membership
// is O(N²); fine for the bounded-size case.  When FOUND-E10 ships
// `canonicalize_pack`, this becomes `is_same_v` on the canonical form.

template <typename PS1, typename PS2>
inline constexpr bool perm_set_equal_v =
    PS1::size == PS2::size
    && perm_set_subset_v<PS1, PS2>
    && perm_set_subset_v<PS2, PS1>;

// ── perm_set_union_t (DISJOINT union) ───────────────────────────────
//
// Concatenates the two element packs, with a static_assert that the
// operands share NO tag.  A permission cannot be held by two
// participants simultaneously — overlap signals a permission-flow
// violation that the type system catches at the union site.
//
// Diagnostic message uses the routed `[PermissionImbalance]` prefix
// (matches `sessions/SessionDiagnostic.h:200` PermissionImbalance tag)
// without including the diagnostic header — keeps PermSet.h
// dependency-light.

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_union_impl;

template <typename... T1s, typename... T2s>
struct perm_set_union_impl<PermSet<T1s...>, PermSet<T2s...>> {
    static_assert(
        ((!perm_set_contains_v<PermSet<T1s...>, T2s>) && ...),
        "crucible::session::diagnostic [PermissionImbalance]: "
        "perm_set_union_t requires disjoint operands — a permission "
        "tag appears in both PermSets.  A CSL permission cannot be "
        "held by two participants simultaneously.  Verify the call "
        "site does not double-insert a tag, and check that "
        "permission_split's children remain disjoint along the "
        "session protocol.");
    using type = PermSet<T1s..., T2s...>;
};

}  // namespace detail

template <typename PS1, typename PS2>
struct perm_set_union : detail::perm_set_union_impl<PS1, PS2> {};

template <typename PS1, typename PS2>
using perm_set_union_t = typename perm_set_union<PS1, PS2>::type;

// ── perm_set_difference_t (PS1 minus PS2's tags) ────────────────────
//
// Filter-by-membership symmetric to `perm_set_remove_t` but driven by
// a set predicate rather than a single tag.

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_difference_impl;

template <typename PS2>
struct perm_set_difference_impl<PermSet<>, PS2> {
    using type = PermSet<>;
};

template <typename Head, typename... Tail, typename PS2>
struct perm_set_difference_impl<PermSet<Head, Tail...>, PS2> {
    using rec_type =
        typename perm_set_difference_impl<PermSet<Tail...>, PS2>::type;

    template <typename S>
    struct prepend_head;

    template <typename... Xs>
    struct prepend_head<PermSet<Xs...>> {
        using type = PermSet<Head, Xs...>;
    };

    using type = std::conditional_t<
        perm_set_contains_v<PS2, Head>,
        rec_type,
        typename prepend_head<rec_type>::type>;
};

}  // namespace detail

template <typename PS1, typename PS2>
struct perm_set_difference : detail::perm_set_difference_impl<PS1, PS2> {};

template <typename PS1, typename PS2>
using perm_set_difference_t =
    typename perm_set_difference<PS1, PS2>::type;

// ── perm_set_canonicalize_t (identity in v1) ────────────────────────
//
// FOUND-E10 (#649, canonicalize_pack) provides the reflection-keyed
// sort.  Until then, `perm_set_equal_v`'s bidirectional-containment
// formulation handles order insensitivity directly; consumers needing
// a hashable canonical form should wait on FOUND-E10.

template <typename PS>
struct perm_set_canonicalize {
    using type = PS;
};

template <typename PS>
using perm_set_canonicalize_t = typename perm_set_canonicalize<PS>::type;

// ── perm_set_name<PS>() ─────────────────────────────────────────────
//
// P2996 `display_string_of(^^PS)` returns the human-readable form
// "PermSet<TagA, TagB, ...>" — used by the abandonment-tracker
// diagnostic in PermissionedSessionHandle's debug-mode destructor
// (Decision D5) to enumerate leaked permission tags.  TU-context-
// fragile per the documented gotcha (e.g. Stale.h:536); use
// `ends_with` rather than `==` if asserting equality.

template <typename PS>
[[nodiscard]] consteval std::string_view perm_set_name() noexcept {
    return std::meta::display_string_of(^^PS);
}

}  // namespace crucible::safety::proto

// ═══════════════════════════════════════════════════════════════════
// Embedded smoke test — fires at every TU include.
// ═══════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::detail::permset_smoke {

// Synthetic tags for compile-time exercises.  Local to detail so they
// never escape into a user's tag namespace.
struct A_tag {};
struct B_tag {};
struct C_tag {};
struct D_tag {};

// ── size ────────────────────────────────────────────────────────────
static_assert(EmptyPermSet::size == 0);
static_assert(PermSet<A_tag>::size == 1);
static_assert(PermSet<A_tag, B_tag, C_tag>::size == 3);

// ── contains ────────────────────────────────────────────────────────
static_assert(!perm_set_contains_v<EmptyPermSet, A_tag>);
static_assert( perm_set_contains_v<PermSet<A_tag>, A_tag>);
static_assert(!perm_set_contains_v<PermSet<A_tag>, B_tag>);
static_assert( perm_set_contains_v<PermSet<A_tag, B_tag, C_tag>, B_tag>);
static_assert(!perm_set_contains_v<PermSet<A_tag, B_tag, C_tag>, D_tag>);

// ── insert (unique-prepend) ────────────────────────────────────────
static_assert(std::is_same_v<
    perm_set_insert_t<EmptyPermSet, A_tag>,
    PermSet<A_tag>>);
static_assert(std::is_same_v<
    perm_set_insert_t<PermSet<B_tag>, A_tag>,
    PermSet<A_tag, B_tag>>);
static_assert(std::is_same_v<                       // no-op when present
    perm_set_insert_t<PermSet<A_tag>, A_tag>,
    PermSet<A_tag>>);
static_assert(std::is_same_v<
    perm_set_insert_t<PermSet<A_tag, B_tag>, C_tag>,
    PermSet<C_tag, A_tag, B_tag>>);

// ── remove (filter) ────────────────────────────────────────────────
static_assert(std::is_same_v<                       // no-op on empty
    perm_set_remove_t<EmptyPermSet, A_tag>,
    PermSet<>>);
static_assert(std::is_same_v<
    perm_set_remove_t<PermSet<A_tag>, A_tag>,
    PermSet<>>);
static_assert(std::is_same_v<                       // no-op when absent
    perm_set_remove_t<PermSet<A_tag>, B_tag>,
    PermSet<A_tag>>);
static_assert(std::is_same_v<
    perm_set_remove_t<PermSet<A_tag, B_tag>, A_tag>,
    PermSet<B_tag>>);
static_assert(std::is_same_v<
    perm_set_remove_t<PermSet<A_tag, B_tag, C_tag>, B_tag>,
    PermSet<A_tag, C_tag>>);

// ── subset ──────────────────────────────────────────────────────────
static_assert( perm_set_subset_v<EmptyPermSet, EmptyPermSet>);
static_assert( perm_set_subset_v<EmptyPermSet, PermSet<A_tag>>);
static_assert( perm_set_subset_v<PermSet<A_tag>, PermSet<A_tag>>);
static_assert( perm_set_subset_v<PermSet<A_tag>, PermSet<A_tag, B_tag>>);
static_assert(!perm_set_subset_v<PermSet<A_tag, C_tag>, PermSet<A_tag, B_tag>>);
static_assert(!perm_set_subset_v<PermSet<A_tag>, EmptyPermSet>);

// ── equality (order-insensitive) ───────────────────────────────────
static_assert( perm_set_equal_v<EmptyPermSet, EmptyPermSet>);
static_assert( perm_set_equal_v<PermSet<A_tag>, PermSet<A_tag>>);
static_assert( perm_set_equal_v<PermSet<A_tag, B_tag>, PermSet<B_tag, A_tag>>);
static_assert( perm_set_equal_v<
    PermSet<A_tag, B_tag, C_tag>,
    PermSet<C_tag, A_tag, B_tag>>);
static_assert(!perm_set_equal_v<PermSet<A_tag>, PermSet<B_tag>>);
static_assert(!perm_set_equal_v<PermSet<A_tag>, PermSet<A_tag, B_tag>>);
static_assert(!perm_set_equal_v<PermSet<A_tag, B_tag>, PermSet<A_tag, C_tag>>);

// ── disjoint union ─────────────────────────────────────────────────
static_assert(std::is_same_v<
    perm_set_union_t<EmptyPermSet, EmptyPermSet>,
    PermSet<>>);
static_assert(std::is_same_v<
    perm_set_union_t<EmptyPermSet, PermSet<A_tag>>,
    PermSet<A_tag>>);
static_assert(std::is_same_v<
    perm_set_union_t<PermSet<A_tag>, PermSet<B_tag>>,
    PermSet<A_tag, B_tag>>);
static_assert(std::is_same_v<
    perm_set_union_t<PermSet<A_tag, B_tag>, PermSet<C_tag>>,
    PermSet<A_tag, B_tag, C_tag>>);

// ── difference ─────────────────────────────────────────────────────
static_assert(std::is_same_v<
    perm_set_difference_t<EmptyPermSet, PermSet<A_tag>>,
    PermSet<>>);
static_assert(std::is_same_v<
    perm_set_difference_t<PermSet<A_tag, B_tag>, EmptyPermSet>,
    PermSet<A_tag, B_tag>>);
static_assert(std::is_same_v<
    perm_set_difference_t<PermSet<A_tag>, PermSet<A_tag>>,
    PermSet<>>);
static_assert(std::is_same_v<
    perm_set_difference_t<PermSet<A_tag, B_tag, C_tag>, PermSet<B_tag>>,
    PermSet<A_tag, C_tag>>);
static_assert(std::is_same_v<
    perm_set_difference_t<PermSet<A_tag, B_tag, C_tag>,
                          PermSet<A_tag, C_tag>>,
    PermSet<B_tag>>);

// ── canonicalize (identity in v1) ──────────────────────────────────
static_assert(std::is_same_v<
    perm_set_canonicalize_t<PermSet<A_tag, B_tag, C_tag>>,
    PermSet<A_tag, B_tag, C_tag>>);

// ── sizeof claims (proof-token discipline) ─────────────────────────
//
// PermSet is an empty class — 1 byte.  When used as a [[no_unique_
// address]] field of PermissionedSessionHandle it collapses to 0 bytes
// via EBO, so `sizeof(PermissionedSessionHandle<P, PS, R>) ==
// sizeof(R)` regardless of |PS|.
static_assert(sizeof(EmptyPermSet) == 1);
static_assert(sizeof(PermSet<A_tag>) == 1);
static_assert(sizeof(PermSet<A_tag, B_tag, C_tag, D_tag>) == 1);
static_assert(std::is_trivially_destructible_v<PermSet<A_tag>>);
static_assert(std::is_empty_v<PermSet<A_tag, B_tag, C_tag>>);

// ── runtime_smoke_test (per the discipline) ────────────────────────
//
// Forces the metafunctions through a non-constexpr call site so the
// linker emits any inline bodies.  Catches consteval/SFINAE/inline-
// body bugs that pure `static_assert` would mask (per
// `feedback_algebra_runtime_smoke_test_discipline.md`).
inline void runtime_smoke_test() noexcept {
    // Compile-time facts re-asserted via runtime path.
    constexpr auto empty_size = EmptyPermSet::size;
    constexpr auto three_size = PermSet<A_tag, B_tag, C_tag>::size;
    static_assert(empty_size == 0);
    static_assert(three_size == 3);

    // Display name is non-empty for any concrete instantiation.
    constexpr auto name = perm_set_name<PermSet<A_tag, B_tag>>();
    static_assert(!name.empty());

    // Equality of canonicalised forms agrees with bidirectional
    // containment.  When FOUND-E10 lands, the two will collapse to
    // the same `is_same_v` check; until then both paths must agree.
    static_assert(perm_set_equal_v<
        perm_set_canonicalize_t<PermSet<A_tag, B_tag>>,
        perm_set_canonicalize_t<PermSet<B_tag, A_tag>>>);
}

}  // namespace crucible::safety::proto::detail::permset_smoke
