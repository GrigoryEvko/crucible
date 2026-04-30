#pragma once

// ── crucible::safety::extract::Reduction ────────────────────────────
//
// FOUND-D14 of 28_04_2026_effects.md §6.1 + 27_04_2026.md §3.3.  The
// THIRD canonical parameter-shape recognizer (after UnaryTransform
// FOUND-D12 and BinaryTransform FOUND-D13) — a function that consumes
// exactly one OwnedRegion<T, Tag>&& (the input) AND borrows exactly
// one reduce_into<R, Op>& (the accumulator).  The reducer folds
// elements of the consumed region into the borrowed accumulator using
// Op; the function's return type must be void because the result
// lives in the borrowed accumulator (the caller keeps it alive across
// iterative refinements).
//
// ── What this header ships ──────────────────────────────────────────
//
//   Reduction<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the reduction shape:
//                          - arity == 2
//                          - parameter 0 is an OwnedRegion<T, Tag>&&
//                            (after cv-ref stripping; non-const)
//                          - parameter 1 is a reduce_into<R, Op>&
//                            (after cv-ref stripping; non-const
//                            lvalue reference — borrowed accumulator)
//                          - return type is void
//
//   is_reduction_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//   reduction_input_tag_t<auto FnPtr>
//                          The input OwnedRegion's Tag.  Used by the
//                          dispatcher to auto-generate the
//                          permission-split that hands per-shard
//                          OwnedRegions to parallel workers.
//
//   reduction_input_value_t<auto FnPtr>
//                          The input OwnedRegion's element type T.
//                          Used to type the per-element callbacks
//                          the dispatcher invokes.
//
//   reduction_accumulator_t<auto FnPtr>
//                          The accumulator's R type (the value the
//                          reducer folds into).  Used by the
//                          dispatcher to allocate per-worker
//                          partial-accumulator wrappers in the
//                          parallel_reduce_views<N, R> lowering.
//
//   reduction_reducer_t<auto FnPtr>
//                          The accumulator's Op type (the
//                          associative reducer).  Used by the
//                          dispatcher to merge per-worker partials
//                          into the caller's accumulator.
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.3 + §5.15 specify that the dispatcher routes Reduction to
// `safety::parallel_reduce_views<N, R>` over the consumed region.
// Per-worker partial accumulators are constructed from the same Op
// (associative by contract); a final reducer pass folds the N
// partials into the caller's accumulator.
//
// Concrete examples that match:
//
//   void sum_into(OwnedRegion<float, X>&&,
//                 reduce_into<float, plus<float>>&);
//   void max_into(OwnedRegion<int,   X>&&,
//                 reduce_into<int,   maxer<int>>&);
//   void hist_into(OwnedRegion<int,  X>&&,
//                  reduce_into<std::array<int, 256>, hist_op>&);
//
// Examples that do NOT match (bug-class catches):
//
//   void f(OwnedRegion<...>&&, reduce_into<...>&&);
//                                    // && on accumulator — would
//                                    // CONSUME the accumulator,
//                                    // defeating iterative refinement
//                                    // (caller wants to keep state
//                                    // alive across calls).
//   void f(OwnedRegion<...>&&, reduce_into<...> const&);
//                                    // const& — accumulator cannot
//                                    // be mutated → reducer cannot
//                                    // make progress.
//   int  f(OwnedRegion<...>&&, reduce_into<...>&);
//                                    // non-void return — reducer
//                                    // result already lives in the
//                                    // borrowed accumulator; a
//                                    // separate return is redundant
//                                    // and ambiguous.
//   void f(OwnedRegion<...>&, reduce_into<...>&);
//                                    // input by lvalue ref —
//                                    // BORROW, not consume; falls
//                                    // through to a different shape
//                                    // (Streaming, FOUND-D-future).
//   void f(reduce_into<...>&, OwnedRegion<...>&&);
//                                    // wrong order — the consumed
//                                    // input is canonically param 0;
//                                    // swapping parameters confuses
//                                    // the dispatcher's permission
//                                    // generation.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Conjunction of seven requires-clauses over signature_traits.  No
// SFINAE-friendly degradation — a function that fails any predicate
// produces a single concept-satisfaction-failure diagnostic naming
// the violated clause.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — the concept is the conjunction of seven orthogonal
//              predicates; no implicit conversion; non-matching
//              signatures fail at the requires-clause boundary.
//   DetSafe — same FnPtr → same recognition result, deterministically;
//              no hidden state.

#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── Reduction concept ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept Reduction =
    arity_v<FnPtr> == 2
    // Parameter 0: non-const rvalue reference to OwnedRegion (the
    // CONSUMED input).  is_owned_region_v applies cv-ref stripping
    // internally; the rvalue-ref + non-const checks happen OUTSIDE
    // the wrapper-detection.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_owned_region_v<param_type_t<FnPtr, 0>>
    // Parameter 1: non-const lvalue reference to reduce_into (the
    // BORROWED accumulator).  Lvalue reference because the caller
    // keeps the accumulator alive across calls; non-const because the
    // reducer must mutate it.
    && std::is_lvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>>
    && is_reduce_into_v<param_type_t<FnPtr, 1>>
    // Return type: void.  The reducer's result lives in the borrowed
    // accumulator; a non-void return would be redundant and ambiguous.
    && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_reduction_v = Reduction<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Extractors ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// All extractors are constrained on Reduction so non-matching
// signatures are rejected at the alias declaration with a single
// requires-clause diagnostic rather than a deep substitution failure.

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_input_tag_t =
    owned_region_tag_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_input_value_t =
    owned_region_value_t<param_type_t<FnPtr, 0>>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_accumulator_t =
    reduce_into_accumulator_t<param_type_t<FnPtr, 1>>;

template <auto FnPtr>
    requires Reduction<FnPtr>
using reduction_reducer_t =
    reduce_into_reducer_t<param_type_t<FnPtr, 1>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// In-header tests cover only the negative cases that don't require
// instantiating OwnedRegion or reduce_into specializations (which
// would expand the include surface).  The sentinel TU
// `test/test_reduction.cpp` ships positive coverage with realistic
// OwnedRegion + reduce_into worked examples.

namespace detail::reduction_self_test {

// Function with no parameters — fails arity == 2.
inline void f_nullary() noexcept {}
static_assert(!Reduction<&f_nullary>);
static_assert(!is_reduction_v<&f_nullary>);

// Function with one parameter — fails arity == 2.
inline void f_unary(int) noexcept {}
static_assert(!Reduction<&f_unary>);

// Function with three parameters — fails arity == 2.
inline void f_ternary(int, int, int) noexcept {}
static_assert(!Reduction<&f_ternary>);

// Function with two non-region/non-reduce_into parameters — fails
// the wrapper-detection clauses.
inline void f_two_ints(int, int) noexcept {}
static_assert(!Reduction<&f_two_ints>);

// Function with int return — fails the void-return clause even if the
// other clauses had been satisfied.
inline int f_returns_int(int, int) noexcept { return 0; }
static_assert(!Reduction<&f_returns_int>);

}  // namespace detail::reduction_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool reduction_smoke_test() noexcept {
    using namespace detail::reduction_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !Reduction<&f_nullary>;
        ok = ok && !Reduction<&f_unary>;
        ok = ok && !Reduction<&f_ternary>;
        ok = ok && !Reduction<&f_two_ints>;
        ok = ok && !Reduction<&f_returns_int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
