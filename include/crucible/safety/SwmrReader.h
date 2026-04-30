#pragma once

// ── crucible::safety::extract::SwmrReader ───────────────────────────
//
// FOUND-D18 of 27_04_2026.md §3.6 + §5.6 + 28_04_2026_effects.md §6.1.
// The SWMR reader-side shape — a function holding a SwmrReader
// handle that loads the current snapshot value and returns it.
//
// ── What this header ships ──────────────────────────────────────────
//
//   SwmrReader<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the SWMR-reader shape:
//                          - arity == 1
//                          - parameter 0 is a non-const rvalue
//                            reference to a SWMR reader handle (a
//                            type satisfying IsSwmrReader from D07)
//                          - return type is non-void AND non-
//                            reference (the loaded snapshot value)
//
//   is_swmr_reader_function_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.  Distinct name from
//                          D07's is_swmr_reader_v (which takes a
//                          handle TYPE, not a function pointer) by
//                          the same convention as D17.
//
//   swmr_reader_handle_value_t<auto FnPtr>
//                          The payload type the handle's load()
//                          returns (equivalent to
//                          swmr_reader_value_t<param 0>).
//
//   swmr_reader_returned_value_t<auto FnPtr>
//                          The function's return type after cv-strip.
//                          Should equal handle_value_t for a well-
//                          formed dispatch.
//
//   swmr_reader_value_consistent_v<auto FnPtr>
//                          Predicate: true iff handle_value_t ==
//                          returned_value_t (after cv-strip).  The
//                          dispatcher requires this to forward the
//                          load() result without conversion.
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.6: the dispatcher routes SwmrReader to a reader-thread
// dispatch that calls `AtomicSnapshot::load()` and forwards the
// result.  Permission tree: a fractional `Reader<UserTag>`
// permission via SharedPermissionPool (N readers concurrently).
//
// Concrete examples that match:
//
//   int    load_int(SwmrReaderSyn&&) noexcept;
//   double load_double(SwmrReaderSyn&&) noexcept;
//   MyStruct load_struct(SwmrReaderSyn&&) noexcept;
//
// Examples that do NOT match:
//
//   void f(SwmrReader&&);                  // void return — that's
//                                          //   not a load
//   int  f(SwmrReader&);                   // handle by lvalue ref
//   int  f(SwmrReader&&, int);             // arity 2 — overlaps
//                                          //   SwmrWriter shape
//   int  f(SwmrWriter&&);                  // wrong handle direction
//   int  f(int);                           // param 0 is not a handle
//   int& f(SwmrReader&&);                  // returning a reference
//                                          //   would mean we've
//                                          //   exposed mutable
//                                          //   storage — reject
//   OwnedRegion<T,X> f(SwmrReader&&);      // loading a region is a
//                                          //   different shape
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Different shape than D15/D16/D17.  Arity 1 (matches UnaryTransform
// in arity but the handle-shape distinguishes), non-void return.
// The framework calls `handle.load()` inside the dispatch.
//
// Reference returns are explicitly rejected: a SWMR reader returning
// `T&` would be unsound — the snapshot's storage is owned by the
// AtomicSnapshot, and exposing a reference would let the caller
// observe torn-state if the writer publishes during the borrow.
// The §3.6 spec is explicit that load returns by value.
//
// OwnedRegion-typed returns are also rejected — a region-yielding
// loader would semantically be a "snapshot window" operation, not
// the SWMR scalar/struct load that AtomicSnapshot supports.
//
// ── Mutual exclusivity ──────────────────────────────────────────────
//
// SwmrReader must NOT collide with:
//   - UnaryTransform — that requires param 0 to be OwnedRegion&&
//     and return to be void or OwnedRegion.  SwmrReader's handle
//     fails OwnedRegion's tag-template specialization match, AND
//     SwmrReader rejects OwnedRegion-typed returns.
//   - SwmrWriter (D17) — that has arity 2 and void return.
//   - ConsumerEndpoint — that has arity 2 with OwnedRegion in
//     param 1.
//   - ProducerEndpoint — that has arity 2.
//
// The exclusion proofs are pinned by the sentinel TU.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — four orthogonal clauses; non-matching signatures
//              fail at the requires-clause boundary.
//   DetSafe — same FnPtr → same recognition result.

#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── SwmrReader concept ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept SwmrReader =
    arity_v<FnPtr> == 1
    // Parameter 0: non-const rvalue reference to a SWMR reader
    // handle (D07 IsSwmrReader on the underlying type).
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_swmr_reader_v<std::remove_cvref_t<param_type_t<FnPtr, 0>>>
    // Return — non-void, non-reference, non-OwnedRegion.  The
    // load() yields a by-value snapshot; reference returns would
    // expose torn state and OwnedRegion returns would mean a
    // different (region-loading) shape.
    && !std::is_void_v<return_type_t<FnPtr>>
    && !std::is_reference_v<return_type_t<FnPtr>>
    && !is_owned_region_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_swmr_reader_function_v = SwmrReader<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Extractors ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// SWMR reader handle's payload type (the type load() returns).
template <auto FnPtr>
    requires SwmrReader<FnPtr>
using swmr_reader_handle_value_t =
    swmr_reader_value_t<std::remove_cvref_t<param_type_t<FnPtr, 0>>>;

// The function's return type after cv-strip.
template <auto FnPtr>
    requires SwmrReader<FnPtr>
using swmr_reader_returned_value_t =
    std::remove_cv_t<return_type_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Value-consistency predicate ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
    requires SwmrReader<FnPtr>
inline constexpr bool swmr_reader_value_consistent_v =
    std::is_same_v<swmr_reader_handle_value_t<FnPtr>,
                   swmr_reader_returned_value_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::swmr_reader_self_test {

// Function with no parameters — fails arity == 1.
inline void f_nullary() noexcept {}
static_assert(!SwmrReader<&f_nullary>);

// Function with two parameters — fails arity == 1.
inline void f_two_ints(int, int) noexcept {}
static_assert(!SwmrReader<&f_two_ints>);

// Function with one non-handle parameter — fails the SWMR-reader
// detection on param 0.
inline int f_one_int(int) noexcept { return 0; }
static_assert(!SwmrReader<&f_one_int>);

// Function with void return — fails the non-void-return clause.
inline void f_void_return_one_int(int) noexcept {}
static_assert(!SwmrReader<&f_void_return_one_int>);

}  // namespace detail::swmr_reader_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool swmr_reader_smoke_test() noexcept {
    using namespace detail::swmr_reader_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !SwmrReader<&f_nullary>;
        ok = ok && !SwmrReader<&f_two_ints>;
        ok = ok && !SwmrReader<&f_one_int>;
        ok = ok && !SwmrReader<&f_void_return_one_int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
