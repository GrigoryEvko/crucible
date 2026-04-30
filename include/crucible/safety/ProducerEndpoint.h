#pragma once

// ── crucible::safety::extract::ProducerEndpoint ─────────────────────
//
// FOUND-D15 of 27_04_2026.md §3.4 + §5.6 + 28_04_2026_effects.md §6.1.
// The FIRST endpoint-shape concept — a function that holds a producer-
// side channel handle and consumes one OwnedRegion of payload data
// into it.
//
// ── What this header ships ──────────────────────────────────────────
//
//   ProducerEndpoint<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the producer-endpoint shape:
//                          - arity == 2
//                          - parameter 0 is a non-const rvalue
//                            reference to a producer handle (a type
//                            satisfying IsProducerHandle from D05)
//                          - parameter 1 is a non-const rvalue
//                            reference to an OwnedRegion<T, Tag>
//                          - return type is void (the canonical
//                            "push and forget" semantic; see §3.4)
//
//   is_producer_endpoint_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//   producer_endpoint_handle_value_t<auto FnPtr>
//                          The payload type the producer handle's
//                          try_push accepts (equivalent to
//                          producer_handle_value_t<param 0>).
//
//   producer_endpoint_region_tag_t<auto FnPtr>
//                          The Tag of the input OwnedRegion.  This
//                          is the SOURCE-SIDE permission tag the
//                          framework will mint for the dispatcher's
//                          producer-thread call.
//
//   producer_endpoint_region_value_t<auto FnPtr>
//                          The element type of the input region.
//                          Should equal handle_value_t for a well-
//                          formed dispatch (see consistency predicate
//                          below).
//
//   producer_endpoint_value_consistent_v<auto FnPtr>
//                          Predicate: true iff handle_value_t ==
//                          region_value_t (after cv-ref strip).  The
//                          §3.4 spec writes both positions as `T` —
//                          the dispatcher requires this to be true,
//                          otherwise a push() would type-mismatch.
//                          The CONCEPT does not require it (we want
//                          the shape recognizer to admit syntactic
//                          matches even when value types disagree;
//                          the dispatcher emits a more specific
//                          diagnostic via this predicate).
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.4: the dispatcher routes ProducerEndpoint to a producer-
// thread context that splits the input region into batches, calls
// `try_push_batch` if available else loops `try_push`.  The
// producer's Permission tag is an auto-generated child of the
// channel's whole-channel tag.  Permission tree auto-generated for
// the input-region Tag via FOUND-D11 inferred_permission_tags_t.
//
// Concrete examples that match:
//
//   void push_into_queue(
//       ProducerHandleSyn&&, OwnedRegion<int, In>&&) noexcept;
//
// Examples that do NOT match:
//
//   void f(ProducerHandle&, OwnedRegion<T, In>&&);    // handle by lvalue ref
//   void f(ProducerHandle&&, OwnedRegion<T, In>&);    // region by lvalue ref
//   void f(ProducerHandle&&);                         // arity 1 — missing region
//   int  f(ProducerHandle&&, OwnedRegion<T, In>&&);   // non-void return
//   void f(ConsumerHandle&&, OwnedRegion<T, In>&&);   // wrong handle direction
//   void f(int, OwnedRegion<T, In>&&);                // param 0 is not a handle
//   void f(ProducerHandle&&, int);                    // param 1 is not a region
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Conjunction of FIVE clauses over signature_traits — same template
// as D13 BinaryTransform, with the per-param wrapper-detection
// predicate swapped:
//
//   D13: param 0 = OwnedRegion, param 1 = OwnedRegion
//   D15: param 0 = ProducerHandle, param 1 = OwnedRegion
//
// Future endpoint shapes (D16 ConsumerEndpoint, D17/D18 SwmrWriter/
// Reader, D19 PipelineStage) follow the same template, swapping the
// per-parameter handle predicate.  Per §3.8, when a function does
// NOT match any canonical shape it falls through to the catch-all.
//
// ── Mutual exclusivity ──────────────────────────────────────────────
//
// ProducerEndpoint must NOT collide with:
//   - UnaryTransform / BinaryTransform — those require both params
//     to be OwnedRegion (D15 has handle in slot 0).
//   - ConsumerEndpoint — that requires param 0 to be a CONSUMER
//     handle, not a producer (D05/D06 are mutually exclusive: a
//     handle exposes either try_push or try_pop, never both).
//   - PipelineStage (§3.7) — that requires param 1 to be a
//     ProducerHandle, not an OwnedRegion.
//
// The exclusion proofs are pinned by the sentinel TU.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — five orthogonal clauses; non-matching signatures
//              fail at the requires-clause boundary.
//   DetSafe — same FnPtr → same recognition result.

#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsProducerHandle.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── ProducerEndpoint concept ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept ProducerEndpoint =
    arity_v<FnPtr> == 2
    // Parameter 0: non-const rvalue reference to a producer handle.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_producer_handle_v<param_type_t<FnPtr, 0>>
    // Parameter 1: non-const rvalue reference to an OwnedRegion.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>>
    && is_owned_region_v<param_type_t<FnPtr, 1>>
    // Return type — void (canonical "push and forget" per §3.4).
    && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_producer_endpoint_v = ProducerEndpoint<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Extractors ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Producer handle's payload type.  Constrained on ProducerEndpoint so
// the alias is ill-formed for non-matching signatures rather than
// returning some void-ish nonsense.
template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
using producer_endpoint_handle_value_t =
    producer_handle_value_t<param_type_t<FnPtr, 0>>;

// Input region's Tag — the source-side permission tag.
template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
using producer_endpoint_region_tag_t =
    owned_region_tag_t<param_type_t<FnPtr, 1>>;

// Input region's element type.  Should equal handle_value_t for a
// well-formed dispatch.
template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
using producer_endpoint_region_value_t =
    owned_region_value_t<param_type_t<FnPtr, 1>>;

// ═════════════════════════════════════════════════════════════════════
// ── Value-consistency predicate ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The §3.4 worked example writes the payload type as `T` in both the
// handle and the region positions.  A well-formed dispatch requires
// these to match — try_push(payload const&) takes the channel's
// element type, and the region is a contiguous run of THAT element
// type.  A mismatch is a semantic error the dispatcher will diagnose
// (more specific than "shape didn't match"), but the SHAPE concept
// admits the syntactic match anyway so the diagnostic can be
// targeted.
//
// Constrained on ProducerEndpoint — the predicate is meaningless on
// non-matching signatures.

template <auto FnPtr>
    requires ProducerEndpoint<FnPtr>
inline constexpr bool producer_endpoint_value_consistent_v =
    std::is_same_v<producer_endpoint_handle_value_t<FnPtr>,
                   producer_endpoint_region_value_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Header self-test covers only negatives that don't require
// instantiating an OwnedRegion specialization or a producer-handle-
// shaped synthetic.  Positive coverage with realistic worked
// examples ships in test/test_producer_endpoint.cpp.

namespace detail::producer_endpoint_self_test {

// Function with no parameters — fails arity == 2.
inline void f_nullary() noexcept {}
static_assert(!ProducerEndpoint<&f_nullary>);

// Function with one parameter — fails arity == 2.
inline void f_one_int(int) noexcept {}
static_assert(!ProducerEndpoint<&f_one_int>);

// Function with two non-handle non-region parameters — fails the
// per-param wrapper-detection clauses.
inline void f_two_ints(int, int) noexcept {}
static_assert(!ProducerEndpoint<&f_two_ints>);

// Function with three parameters — fails arity == 2.
inline void f_three_params(int, int, int) noexcept {}
static_assert(!ProducerEndpoint<&f_three_params>);

}  // namespace detail::producer_endpoint_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool producer_endpoint_smoke_test() noexcept {
    using namespace detail::producer_endpoint_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !ProducerEndpoint<&f_nullary>;
        ok = ok && !ProducerEndpoint<&f_one_int>;
        ok = ok && !ProducerEndpoint<&f_two_ints>;
        ok = ok && !ProducerEndpoint<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
