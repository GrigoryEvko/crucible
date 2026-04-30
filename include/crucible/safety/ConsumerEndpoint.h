#pragma once

// ── crucible::safety::extract::ConsumerEndpoint ─────────────────────
//
// FOUND-D16 of 27_04_2026.md §3.5 + §5.6 + 28_04_2026_effects.md §6.1.
// Symmetric to D15 ProducerEndpoint — a function that holds a
// consumer-side channel handle and writes drained payload data into
// one OwnedRegion.
//
// ── What this header ships ──────────────────────────────────────────
//
//   ConsumerEndpoint<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the consumer-endpoint shape:
//                          - arity == 2
//                          - parameter 0 is a non-const rvalue
//                            reference to a consumer handle (a type
//                            satisfying IsConsumerHandle from D06)
//                          - parameter 1 is a non-const rvalue
//                            reference to an OwnedRegion<T, Tag>
//                          - return type is void (the canonical
//                            "drain into output buffer" semantic;
//                            see §3.5)
//
//   is_consumer_endpoint_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//   consumer_endpoint_handle_value_t<auto FnPtr>
//                          The payload type the consumer handle's
//                          try_pop yields (equivalent to
//                          consumer_handle_value_t<param 0>).
//
//   consumer_endpoint_region_tag_t<auto FnPtr>
//                          The Tag of the output OwnedRegion.  This
//                          is the SINK-SIDE permission tag the
//                          framework will mint for the dispatcher's
//                          consumer-thread call.
//
//   consumer_endpoint_region_value_t<auto FnPtr>
//                          The element type of the output region.
//                          Should equal handle_value_t for a well-
//                          formed dispatch (see consistency predicate
//                          below).
//
//   consumer_endpoint_value_consistent_v<auto FnPtr>
//                          Predicate: true iff handle_value_t ==
//                          region_value_t (after cv-ref strip).  The
//                          §3.5 spec writes both positions as `T` —
//                          the dispatcher requires this to be true,
//                          otherwise drained payloads couldn't be
//                          written into the region.  The CONCEPT
//                          does not require it (we want the shape
//                          recognizer to admit syntactic matches
//                          even when value types disagree; the
//                          dispatcher emits a more specific
//                          diagnostic via this predicate).
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.5: the dispatcher routes ConsumerEndpoint to a consumer-
// thread loop that drives `try_pop_batch` where available, falling
// back to `try_pop` loops; framework handles drain-on-shutdown
// semantics.  Permission tree auto-generated for the output-region
// Tag via FOUND-D11 inferred_permission_tags_t.
//
// Concrete examples that match:
//
//   void drain_from_queue(
//       ConsumerHandleSyn&&, OwnedRegion<int, Out>&&) noexcept;
//
// Examples that do NOT match:
//
//   void f(ConsumerHandle&, OwnedRegion<T, Out>&&);    // handle by lvalue ref
//   void f(ConsumerHandle&&, OwnedRegion<T, Out>&);    // region by lvalue ref
//   void f(ConsumerHandle&&);                          // arity 1 — missing region
//   int  f(ConsumerHandle&&, OwnedRegion<T, Out>&&);   // non-void return
//   void f(ProducerHandle&&, OwnedRegion<T, Out>&&);   // wrong handle direction
//   void f(int, OwnedRegion<T, Out>&&);                // param 0 is not a handle
//   void f(ConsumerHandle&&, int);                     // param 1 is not a region
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Conjunction of FIVE clauses over signature_traits — same template
// as D15 ProducerEndpoint, with the per-param wrapper-detection
// predicate swapped:
//
//   D15: param 0 = ProducerHandle, param 1 = OwnedRegion
//   D16: param 0 = ConsumerHandle, param 1 = OwnedRegion
//
// Future endpoint shapes (D17/D18 SwmrWriter/Reader, D19
// PipelineStage) follow the same template, swapping the per-
// parameter handle predicate.
//
// ── Mutual exclusivity ──────────────────────────────────────────────
//
// ConsumerEndpoint must NOT collide with:
//   - UnaryTransform / BinaryTransform — those require both params
//     to be OwnedRegion (D16 has handle in slot 0).
//   - ProducerEndpoint — D05/D06 are mutually exclusive: a handle
//     exposes either try_push or try_pop, never both (the D05 and
//     D06 predicates explicitly require the OTHER method to be
//     absent).
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

#include <crucible/safety/IsConsumerHandle.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── ConsumerEndpoint concept ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept ConsumerEndpoint =
    arity_v<FnPtr> == 2
    // Parameter 0: non-const rvalue reference to a consumer handle.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_consumer_handle_v<param_type_t<FnPtr, 0>>
    // Parameter 1: non-const rvalue reference to an OwnedRegion.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>>
    && is_owned_region_v<param_type_t<FnPtr, 1>>
    // Return type — void (canonical "drain into output buffer" per
    // §3.5).
    && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_consumer_endpoint_v = ConsumerEndpoint<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Extractors ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Consumer handle's payload type.  Constrained on ConsumerEndpoint
// so the alias is ill-formed for non-matching signatures rather than
// returning some void-ish nonsense.
template <auto FnPtr>
    requires ConsumerEndpoint<FnPtr>
using consumer_endpoint_handle_value_t =
    consumer_handle_value_t<param_type_t<FnPtr, 0>>;

// Output region's Tag — the sink-side permission tag.
template <auto FnPtr>
    requires ConsumerEndpoint<FnPtr>
using consumer_endpoint_region_tag_t =
    owned_region_tag_t<param_type_t<FnPtr, 1>>;

// Output region's element type.  Should equal handle_value_t for a
// well-formed dispatch.
template <auto FnPtr>
    requires ConsumerEndpoint<FnPtr>
using consumer_endpoint_region_value_t =
    owned_region_value_t<param_type_t<FnPtr, 1>>;

// ═════════════════════════════════════════════════════════════════════
// ── Value-consistency predicate ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The §3.5 worked example writes the payload type as `T` in both
// the handle and the region positions.  A well-formed dispatch
// requires these to match — try_pop yields the channel's element
// type, and the region is a contiguous run of THAT element type.
// A mismatch is a semantic error the dispatcher will diagnose
// (more specific than "shape didn't match"), but the SHAPE concept
// admits the syntactic match anyway so the diagnostic can be
// targeted.
//
// Constrained on ConsumerEndpoint — the predicate is meaningless
// on non-matching signatures.

template <auto FnPtr>
    requires ConsumerEndpoint<FnPtr>
inline constexpr bool consumer_endpoint_value_consistent_v =
    std::is_same_v<consumer_endpoint_handle_value_t<FnPtr>,
                   consumer_endpoint_region_value_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Header self-test covers only negatives that don't require
// instantiating an OwnedRegion specialization or a consumer-handle-
// shaped synthetic.  Positive coverage with realistic worked
// examples ships in test/test_consumer_endpoint.cpp.

namespace detail::consumer_endpoint_self_test {

// Function with no parameters — fails arity == 2.
inline void f_nullary() noexcept {}
static_assert(!ConsumerEndpoint<&f_nullary>);

// Function with one parameter — fails arity == 2.
inline void f_one_int(int) noexcept {}
static_assert(!ConsumerEndpoint<&f_one_int>);

// Function with two non-handle non-region parameters — fails the
// per-param wrapper-detection clauses.
inline void f_two_ints(int, int) noexcept {}
static_assert(!ConsumerEndpoint<&f_two_ints>);

// Function with three parameters — fails arity == 2.
inline void f_three_params(int, int, int) noexcept {}
static_assert(!ConsumerEndpoint<&f_three_params>);

}  // namespace detail::consumer_endpoint_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool consumer_endpoint_smoke_test() noexcept {
    using namespace detail::consumer_endpoint_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !ConsumerEndpoint<&f_nullary>;
        ok = ok && !ConsumerEndpoint<&f_one_int>;
        ok = ok && !ConsumerEndpoint<&f_two_ints>;
        ok = ok && !ConsumerEndpoint<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
