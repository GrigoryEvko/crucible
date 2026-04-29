#pragma once

// ── crucible::safety::extract::BinaryTransform ──────────────────────
//
// FOUND-D13 of 28_04_2026_effects.md §6.1 + 27_04_2026.md §3.2.
// The SECOND canonical parameter-shape recognizer — a function that
// consumes exactly TWO OwnedRegion<T, Tag>&& parameters and produces
// either void (in-place against the first input) or another
// OwnedRegion (out-of-place output).
//
// ── What this header ships ──────────────────────────────────────────
//
//   BinaryTransform<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the binary-transform shape:
//                          - arity == 2
//                          - both parameters are non-const rvalue
//                            references to OwnedRegion<T, Tag>
//                          - return type is either void or another
//                            OwnedRegion<U, OutTag>
//
//   is_binary_transform_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//   is_in_place_binary_transform_v<auto FnPtr>
//                          Refinement: true iff BinaryTransform AND
//                          return_type is void.  The dispatcher
//                          treats in-place against parameter 0
//                          (the lhs region is reused as output;
//                          rhs is consumed and discarded).
//
//   binary_transform_lhs_tag_t<auto FnPtr>
//                          The first input OwnedRegion's Tag.
//                          (Tag for parameter 0.)
//
//   binary_transform_rhs_tag_t<auto FnPtr>
//                          The second input OwnedRegion's Tag.
//                          (Tag for parameter 1.)  May or may not
//                          equal lhs_tag — both cases are valid.
//
//   binary_transform_lhs_value_t<auto FnPtr>
//                          The first input region's element type.
//
//   binary_transform_rhs_value_t<auto FnPtr>
//                          The second input region's element type.
//
//   binary_transform_output_tag_t<auto FnPtr>
//                          The output region's Tag, OR `void` when
//                          the transform is in-place.
//
//   binary_transform_has_same_tag_v<auto FnPtr>
//                          Predicate: true iff lhs_tag == rhs_tag
//                          (after cv-ref strip).  The dispatcher
//                          uses this to choose between single-
//                          permission-pool (same tag → one
//                          splits_into_pack group) and dual-pool
//                          (distinct tags → two groups) lowerings.
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.2 + §5.15 specify that the dispatcher routes
// BinaryTransform to `safety::parallel_apply_pair<N>(rA, rB, body)`
// — paired iteration with element-count match enforced by a
// `pre()` contract on the dispatch boundary.  Permission tree
// auto-generated for both Tags via FOUND-D11
// inferred_permission_tags_t.
//
// Concrete examples that match:
//
//   void merge_in_place(OwnedRegion<float, A>&&, OwnedRegion<float, B>&&);
//   OwnedRegion<float, C> add(OwnedRegion<float, A>&&, OwnedRegion<float, B>&&);
//   void merge_same_tag(OwnedRegion<float, A>&&, OwnedRegion<int, A>&&);  // same tag, two slices
//
// Examples that do NOT match:
//
//   void f(OwnedRegion<T, A>&, OwnedRegion<U, B>&&);   // first param is borrow, not consume
//   void f(OwnedRegion<T, A>&&, OwnedRegion<U, B>);    // second param by-value
//   void f(OwnedRegion<T, A>&&);                       // arity 1 — UnaryTransform, not Binary
//   int  f(OwnedRegion<T, A>&&, OwnedRegion<U, B>&&);  // non-region non-void return
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Conjunction of FIVE clauses over signature_traits.  Mechanical
// extension of UnaryTransform (D12) — arity doubled, predicates
// applied to BOTH parameter positions.  Future shape concepts
// (D14 Reduction, D15-D18 endpoints, D19 PipelineStage) follow
// the same template, swapping the per-parameter predicates.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — five orthogonal clauses; non-matching signatures
//              fail at the requires-clause boundary.
//   DetSafe — same FnPtr → same recognition result.

#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/IsOwnedRegion.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── BinaryTransform concept ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept BinaryTransform =
    arity_v<FnPtr> == 2
    // Parameter 0: non-const rvalue reference to OwnedRegion.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_owned_region_v<param_type_t<FnPtr, 0>>
    // Parameter 1: non-const rvalue reference to OwnedRegion.
    // (Same constraint shape as parameter 0; two distinct OwnedRegions
    // may share a tag or differ.)
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>>
    && is_owned_region_v<param_type_t<FnPtr, 1>>
    // Return type — void (in-place against lhs) OR OwnedRegion
    // (out-of-place output).
    && (std::is_void_v<return_type_t<FnPtr>>
        || is_owned_region_v<return_type_t<FnPtr>>);

template <auto FnPtr>
inline constexpr bool is_binary_transform_v = BinaryTransform<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Refinement + extractors ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
inline constexpr bool is_in_place_binary_transform_v =
    BinaryTransform<FnPtr> && std::is_void_v<return_type_t<FnPtr>>;

// LHS / RHS region tags — constrained on BinaryTransform.
template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_lhs_tag_t =
    owned_region_tag_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_rhs_tag_t =
    owned_region_tag_t<param_type_t<FnPtr, 1>>;

// LHS / RHS element types.
template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_lhs_value_t =
    owned_region_value_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_rhs_value_t =
    owned_region_value_t<param_type_t<FnPtr, 1>>;

// Output region's Tag, or `void` when in-place.  Same dispatcher
// pattern as UnaryTransform — discriminate the void case to avoid
// substitution failure on the void branch.
namespace detail {

template <auto FnPtr, bool IsInPlace>
struct binary_transform_output_tag_select;

template <auto FnPtr>
struct binary_transform_output_tag_select<FnPtr, /*IsInPlace=*/true> {
    using type = void;
};

template <auto FnPtr>
struct binary_transform_output_tag_select<FnPtr, /*IsInPlace=*/false> {
    using type = owned_region_tag_t<return_type_t<FnPtr>>;
};

}  // namespace detail

template <auto FnPtr>
    requires BinaryTransform<FnPtr>
using binary_transform_output_tag_t = typename
    detail::binary_transform_output_tag_select<
        FnPtr, std::is_void_v<return_type_t<FnPtr>>>::type;

// Same-tag predicate.  Constrained on BinaryTransform so non-
// matching signatures are rejected at the variable-template's
// `requires` clause.
template <auto FnPtr>
    requires BinaryTransform<FnPtr>
inline constexpr bool binary_transform_has_same_tag_v =
    std::is_same_v<binary_transform_lhs_tag_t<FnPtr>,
                   binary_transform_rhs_tag_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Header self-test covers only negatives that don't require
// instantiating an OwnedRegion specialization.  The sentinel TU
// `test/test_binary_transform.cpp` ships positive coverage with
// realistic OwnedRegion-typed worked examples.

namespace detail::binary_transform_self_test {

// Function with no parameters — fails arity == 2.
inline void f_nullary() noexcept {}
static_assert(!BinaryTransform<&f_nullary>);

// Function with one parameter — fails arity == 2 (would match
// UnaryTransform's arity check).
inline void f_one_int(int) noexcept {}
static_assert(!BinaryTransform<&f_one_int>);

// Function with two non-region parameters — fails the per-param
// is_owned_region_v clauses.
inline void f_two_ints(int, int) noexcept {}
static_assert(!BinaryTransform<&f_two_ints>);

// Function with three parameters — fails arity == 2.
inline void f_three_params(int, int, int) noexcept {}
static_assert(!BinaryTransform<&f_three_params>);

}  // namespace detail::binary_transform_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool binary_transform_smoke_test() noexcept {
    using namespace detail::binary_transform_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !BinaryTransform<&f_nullary>;
        ok = ok && !BinaryTransform<&f_one_int>;
        ok = ok && !BinaryTransform<&f_two_ints>;
        ok = ok && !BinaryTransform<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
