#pragma once

// ── crucible::safety::extract::UnaryTransform ───────────────────────
//
// FOUND-D12 of 28_04_2026_effects.md §6.1 + 27_04_2026.md §3.1.  The
// FIRST canonical parameter-shape recognizer — a function that
// consumes exactly one OwnedRegion<T, Tag>&& and produces either
// void (in-place transform) or another OwnedRegion (out-of-place).
//
// ── What this header ships ──────────────────────────────────────────
//
//   UnaryTransform<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the unary-transform shape:
//                          - arity == 1
//                          - parameter 0 is an OwnedRegion<T, Tag>
//                            after cv-ref stripping
//                          - parameter 0 is a NON-CONST rvalue
//                            reference (the &&-consumed shape; const&&
//                            is rejected because you cannot move from
//                            a const value, defeating the consume-
//                            ownership semantics)
//                          - return type is either void or another
//                            OwnedRegion<U, OutTag>
//
//   is_unary_transform_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds where a value is more
//                          ergonomic than a concept.
//
//   is_in_place_unary_transform_v<auto FnPtr>
//                          Refinement: true iff UnaryTransform AND
//                          return_type is void.  Used by the
//                          dispatcher to pick between single-buffer
//                          (in-place) and double-buffer (out-of-place)
//                          lowerings.
//
//   unary_transform_input_tag_t<auto FnPtr>
//                          The input OwnedRegion's Tag (extracted
//                          from parameter 0 after cv-ref stripping).
//                          Used to auto-generate splits_into_pack at
//                          dispatch time.
//
//   unary_transform_input_value_t<auto FnPtr>
//                          The input OwnedRegion's element type T.
//                          Used to type the per-element callbacks
//                          the dispatcher invokes.
//
//   unary_transform_output_tag_t<auto FnPtr>
//                          The output OwnedRegion's Tag, OR `void`
//                          when the transform is in-place (return
//                          type is void).  Used for output region
//                          allocation and permission emission.
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.1 + §5.15 specify that the dispatcher routes UnaryTransform
// to `safety::parallel_for_views<N>` over the consumed region.  The
// permission tree is auto-generated from the inferred Tag (FOUND-D11);
// N is selected by the cache-tier rule from the region's working-set
// bytes (CostModel.h).
//
// Concrete examples that match:
//
//   void normalize(OwnedRegion<float, Inputs>&&);              // in-place
//   OwnedRegion<int, Outputs> classify(OwnedRegion<float, Inputs>&&); // out-of-place
//
// Examples that do NOT match (bug-class catches):
//
//   void f(OwnedRegion<T, Tag>&);    // not rvalue-reference — caller
//                                    // retains ownership; this is a
//                                    // BORROW, not a unary transform.
//   void f(OwnedRegion<T, Tag>);     // by-value — slicing risk; the
//                                    // dispatcher refuses to silently
//                                    // copy a region.
//   void f(int, OwnedRegion<...>&&); // arity 2 — not unary.
//   int  f(OwnedRegion<...>&&);      // non-void non-region return —
//                                    // not part of the transform
//                                    // shape; falls through to the
//                                    // catch-all (D20 NonCanonical).
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Conjunction of three requires-clauses over signature_traits.  No
// SFINAE-friendly degradation — a function that fails any predicate
// produces a single concept-satisfaction-failure diagnostic naming
// the violated clause.  This is the load-bearing diagnostic
// improvement over a hand-written predicate that would surface as
// a 200-line template substitution failure at the dispatch site.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — the concept is the conjunction of three orthogonal
//              predicates; no implicit conversion; non-matching
//              signatures fail at the requires-clause boundary.
//   DetSafe — same FnPtr → same recognition result, deterministically;
//              no hidden state.

#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/IsOwnedRegion.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── UnaryTransform concept ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept UnaryTransform =
    arity_v<FnPtr> == 1
    // Parameter 0's reference category — must be rvalue reference,
    // expressing the consume-ownership semantics.  is_owned_region_v
    // applies cv-ref stripping internally, so we check rvalue-ref
    // OUTSIDE the wrapper-detection.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    // CONST rvalue reference is rejected — you cannot move from a
    // const value, defeating consume-ownership semantics.  We check
    // this by stripping ONE reference level (turning && into the
    // pointee) and verifying the pointee is non-const.
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    // Parameter 0's stripped type — must be OwnedRegion<T, Tag>.
    && is_owned_region_v<param_type_t<FnPtr, 0>>
    // Return type — void (in-place) OR OwnedRegion (out-of-place).
    && (std::is_void_v<return_type_t<FnPtr>>
        || is_owned_region_v<return_type_t<FnPtr>>);

template <auto FnPtr>
inline constexpr bool is_unary_transform_v = UnaryTransform<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Refinement + extractors ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// In-place vs out-of-place refinement.  In-place transforms have
// void return and need only one region; out-of-place transforms
// allocate a new output region with potentially different tag /
// element type.
template <auto FnPtr>
inline constexpr bool is_in_place_unary_transform_v =
    UnaryTransform<FnPtr> && std::is_void_v<return_type_t<FnPtr>>;

// Input region's Tag.  Constrained on UnaryTransform so non-matching
// signatures are rejected at the alias declaration with a single
// requires-clause diagnostic rather than a deep substitution failure.
template <auto FnPtr>
    requires UnaryTransform<FnPtr>
using unary_transform_input_tag_t =
    owned_region_tag_t<param_type_t<FnPtr, 0>>;

// Input region's element type T.
template <auto FnPtr>
    requires UnaryTransform<FnPtr>
using unary_transform_input_value_t =
    owned_region_value_t<param_type_t<FnPtr, 0>>;

// Output region's Tag, OR `void` when the transform is in-place.
// We use a detail-namespace dispatcher to discriminate the void
// case without creating a substitution failure on the void branch.
namespace detail {

template <auto FnPtr, bool IsInPlace>
struct unary_transform_output_tag_select;

template <auto FnPtr>
struct unary_transform_output_tag_select<FnPtr, /*IsInPlace=*/true> {
    using type = void;
};

template <auto FnPtr>
struct unary_transform_output_tag_select<FnPtr, /*IsInPlace=*/false> {
    using type = owned_region_tag_t<return_type_t<FnPtr>>;
};

}  // namespace detail

template <auto FnPtr>
    requires UnaryTransform<FnPtr>
using unary_transform_output_tag_t = typename
    detail::unary_transform_output_tag_select<
        FnPtr, std::is_void_v<return_type_t<FnPtr>>>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// In-header tests cover only the negative cases that don't require
// instantiating an OwnedRegion specialization (which would force
// inclusion of OwnedRegion.h here, expanding the include surface).
// The sentinel TU `test/test_unary_transform.cpp` ships positive
// coverage with realistic OwnedRegion-typed worked examples.

namespace detail::unary_transform_self_test {

// Function with no parameters — fails arity == 1.
inline void f_nullary() noexcept {}
static_assert(!UnaryTransform<&f_nullary>);
static_assert(!is_unary_transform_v<&f_nullary>);

// Function with non-region parameter — fails is_owned_region_v.
inline void f_int(int) noexcept {}
static_assert(!UnaryTransform<&f_int>);

// Function with two parameters — fails arity == 1.
inline void f_two_ints(int, int) noexcept {}
static_assert(!UnaryTransform<&f_two_ints>);

// Function with int return — fails the return-type clause.
inline int f_returns_int(int) noexcept { return 0; }
static_assert(!UnaryTransform<&f_returns_int>);

}  // namespace detail::unary_transform_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool unary_transform_smoke_test() noexcept {
    using namespace detail::unary_transform_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !UnaryTransform<&f_nullary>;
        ok = ok && !UnaryTransform<&f_int>;
        ok = ok && !UnaryTransform<&f_two_ints>;
        ok = ok && !UnaryTransform<&f_returns_int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
