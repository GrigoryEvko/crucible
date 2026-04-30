#pragma once

// ── crucible::safety::extract::SwmrWriter ───────────────────────────
//
// FOUND-D17 of 27_04_2026.md §3.6 + §5.6 + 28_04_2026_effects.md §6.1.
// The SWMR (single-writer-multiple-reader) writer-side shape — a
// function holding a SwmrWriter handle that publishes a single
// scalar value into the channel.
//
// ── What this header ships ──────────────────────────────────────────
//
//   SwmrWriter<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the SWMR-writer shape:
//                          - arity == 2
//                          - parameter 0 is a non-const rvalue
//                            reference to a SWMR writer handle (a
//                            type satisfying IsSwmrWriter from D07)
//                          - parameter 1 is a non-reference,
//                            non-OwnedRegion, non-handle type — the
//                            published value (by-value per §3.6)
//                          - return type is void
//
//   is_swmr_writer_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//                          NOTE: collides in spelling with D07's
//                          `is_swmr_writer_v<T>` predicate on a
//                          handle type.  Per the same convention as
//                          D15 ProducerEndpoint vs D05's
//                          is_producer_handle_v, we differentiate
//                          via the namespace+template-arg kind: D07
//                          takes a TYPE; D17 takes an `auto FnPtr`
//                          NTTP.  C++ overload resolution uses the
//                          template-parameter-list form to pick the
//                          right one, so they coexist in one
//                          namespace without ambiguity.  We
//                          intentionally pick a different name to
//                          avoid even cosmetic confusion:
//                          `is_swmr_writer_function_v`.
//
//   swmr_writer_handle_value_t<auto FnPtr>
//                          The payload type the SWMR writer's
//                          publish accepts (equivalent to
//                          swmr_writer_value_t<param 0>).
//
//   swmr_writer_published_value_t<auto FnPtr>
//                          The type of param 1 after cv-ref strip —
//                          the type of the value the user passes.
//                          Should equal handle_value_t for a well-
//                          formed dispatch.
//
//   swmr_writer_value_consistent_v<auto FnPtr>
//                          Predicate: true iff handle_value_t ==
//                          published_value_t (after cv-ref strip).
//                          Required for the dispatcher to make the
//                          publish() call without a conversion.  The
//                          CONCEPT admits even when types disagree
//                          (so the dispatcher can emit a more
//                          specific diagnostic via this predicate).
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.6: the dispatcher routes SwmrWriter to a writer-thread
// dispatch that calls `AtomicSnapshot::publish(value)`.  Permission
// tree: the writer's tag is a `Writer<UserTag>` linear permission;
// readers (separate D18 SwmrReader matches) get fractional
// `Reader<UserTag>` permissions through a SharedPermissionPool.
//
// Concrete examples that match:
//
//   void publish_int(SwmrWriterSyn&&, int value) noexcept;
//   void publish_struct(SwmrWriterSyn&&, MyStruct value) noexcept;
//
// Examples that do NOT match:
//
//   void f(SwmrWriter&, int);              // handle by lvalue ref
//   void f(SwmrWriter&&);                  // arity 1 — missing value
//   int  f(SwmrWriter&&, int);             // non-void return
//   void f(SwmrReader&&, int);             // wrong handle direction
//   void f(int, SwmrWriter&&);             // handle in wrong slot
//   void f(SwmrWriter&&, int&);            // value by lvalue ref
//   void f(SwmrWriter&&, int&&);           // value by rvalue ref
//   void f(SwmrWriter&&, OwnedRegion<T,X>&&);  // overlaps ProducerEndpoint
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Different shape than D15/D16 endpoint concepts.  D15/D16 take
// (Handle&&, OwnedRegion&&) — two consume-by-rvalue-ref params.
// D17 takes (Handle&&, T value) — handle is consumed, value is
// passed by-value.  The framework calls `handle.publish(value)`
// inside the dispatch.
//
// The §3.6 spec is explicit about by-value semantics: a SWMR
// publish operates on a single scalar/struct, NOT a contiguous
// region.  Reject reference-shaped param 1 to keep this contract.
// Reject OwnedRegion specifically to avoid silent overlap with
// ProducerEndpoint when someone writes
// `void f(SwmrWriter&&, OwnedRegion&&)` thinking either shape would
// admit it.
//
// ── Mutual exclusivity ──────────────────────────────────────────────
//
// SwmrWriter must NOT collide with:
//   - ProducerEndpoint / ConsumerEndpoint — those require param 1
//     to be OwnedRegion&&; SwmrWriter explicitly rejects that.
//   - SwmrReader (D18) — that has arity 1 and non-void return.
//   - UnaryTransform / BinaryTransform — those require param 0 to
//     be OwnedRegion&&; SwmrWriter has a SWMR handle there, which
//     fails OwnedRegion's tag-template specialization match.
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
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── SwmrWriter concept ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept SwmrWriter =
    arity_v<FnPtr> == 2
    // Parameter 0: non-const rvalue reference to a SWMR writer
    // handle (D07 IsSwmrWriter on the underlying type).
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_swmr_writer_v<std::remove_cvref_t<param_type_t<FnPtr, 0>>>
    // Parameter 1: by-value (NOT a reference) AND NOT an
    // OwnedRegion (to preserve mutual exclusion with
    // ProducerEndpoint).  Pointers are not categorically rejected
    // here — a pointer-to-T is `T*`, which is also "by value of
    // pointer type" and admissible per §3.6's literal `T value`
    // wording.  The dispatcher will then call publish(value) which
    // will pass the pointer through.
    && !std::is_reference_v<param_type_t<FnPtr, 1>>
    && !is_owned_region_v<param_type_t<FnPtr, 1>>
    // Return type — void per §3.6 ("publish and forget").
    && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_swmr_writer_function_v = SwmrWriter<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Extractors ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// SWMR writer handle's payload type (the type publish accepts).
template <auto FnPtr>
    requires SwmrWriter<FnPtr>
using swmr_writer_handle_value_t =
    swmr_writer_value_t<std::remove_cvref_t<param_type_t<FnPtr, 0>>>;

// The user-passed value type at param 1, after cv-ref strip.
template <auto FnPtr>
    requires SwmrWriter<FnPtr>
using swmr_writer_published_value_t =
    std::remove_cv_t<param_type_t<FnPtr, 1>>;

// ═════════════════════════════════════════════════════════════════════
// ── Value-consistency predicate ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// §3.6 writes the user-function's value parameter as `T value` and
// the publish-method's param as `P const&` where T == P.  A well-
// formed dispatch requires these to match exactly (after cv-ref
// strip) — otherwise a conversion would happen at the publish call
// site, which the dispatcher will diagnose more specifically via
// this predicate.

template <auto FnPtr>
    requires SwmrWriter<FnPtr>
inline constexpr bool swmr_writer_value_consistent_v =
    std::is_same_v<swmr_writer_handle_value_t<FnPtr>,
                   swmr_writer_published_value_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::swmr_writer_self_test {

// Function with no parameters — fails arity == 2.
inline void f_nullary() noexcept {}
static_assert(!SwmrWriter<&f_nullary>);

// Function with one parameter — fails arity == 2.
inline void f_one_int(int) noexcept {}
static_assert(!SwmrWriter<&f_one_int>);

// Function with two non-handle parameters — fails the SWMR-writer
// detection on param 0.
inline void f_two_ints(int, int) noexcept {}
static_assert(!SwmrWriter<&f_two_ints>);

// Function with three parameters — fails arity == 2.
inline void f_three_params(int, int, int) noexcept {}
static_assert(!SwmrWriter<&f_three_params>);

}  // namespace detail::swmr_writer_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool swmr_writer_smoke_test() noexcept {
    using namespace detail::swmr_writer_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !SwmrWriter<&f_nullary>;
        ok = ok && !SwmrWriter<&f_one_int>;
        ok = ok && !SwmrWriter<&f_two_ints>;
        ok = ok && !SwmrWriter<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
