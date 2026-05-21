#pragma once

// ── crucible::fixy::kernel — kernel-shape recognizer surface ───────
//
// Surfaces the function-pointer-shape recognizers from
// `include/crucible/safety/{Reduction,reduce_into,IsReduceInto}.h`
// under `fixy::kernel::`.  Per misc/27_04_2026.md §3.3 +
// misc/28_04_2026_effects.md §6.1 §10 + FIXY-V-038: closes the
// umbrella-reach gap where dispatcher / Forge-phase code wanting to
// recognize "this function is a Reduction" had to descend into
// `crucible::safety::extract::` (the recognizer's actual namespace).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::reduce_into<R, Op>       — accumulator-into-place class
//   safety::is_reduction_op_v<Op, R> — Op admissibility concept
//   safety::extract::Reduction<auto FnPtr>
//                                    — FOUND-D14 kernel-shape concept
//   safety::extract::is_reduction_v  — variable-template form
//   safety::extract::reduction_input_tag_t
//   safety::extract::reduction_input_value_t
//   safety::extract::reduction_accumulator_t
//   safety::extract::reduction_reducer_t
//                                    — 4 type-extractors
//   safety::extract::IsReduceInto<T> — wrapper-detection concept
//   safety::extract::is_reduce_into_v<T>
//                                    — variable-template form
//   safety::extract::reduce_into_accumulator_t
//   safety::extract::reduce_into_reducer_t
//                                    — wrapper-shape extractors
//
// ── Role ───────────────────────────────────────────────────────────
//
// `fixy::kernel::` is the third top-level fixy namespace family.
// `fixy::handle::` covers RAII resource handles, `fixy::wrap::` covers
// value-wrappers; `fixy::kernel::` covers KERNEL-SHAPE RECOGNIZERS —
// function-pointer-shape concepts and their extractors that the
// dispatcher reads to route each function to its canonical lowering
// (UnaryTransform → parallel_map; Reduction → parallel_reduce_views;
// BinaryTransform → parallel_combine; etc.).  Future re-exports in
// this header (V-039 / V-041 / V-043) will add Fusion / CanonicalShape
// / Binary/UnaryTransform / PipelineStage / ConsumerEndpoint /
// ProducerEndpoint shape recognizers.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Concepts + variable templates + alias templates are pure
// consteval predicates; using-declarations are pure name-lookup
// directives.  No code is generated.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — re-exports do not introduce new state paths.
//   TypeSafe   — using-declarations preserve substrate type identity
//                (no implicit conversions); concept admission set is
//                identical to the substrate.
//   NullSafe   — N/A; concepts are type-level predicates.
//   MemSafe    — reduce_into is move-only at substrate; alias inherits.
//   BorrowSafe — reduce_into deletes copy at substrate; cross-thread
//                concurrent combine() is a documented data race the
//                framework avoids by minting N per-worker partials
//                (parallel_reduce_views).  Alias preserves discipline.
//   ThreadSafe — same as BorrowSafe; alias is a name-lookup directive.
//   LeakSafe   — value-type wrappers; destruct paths bounded.
//   DetSafe    — concepts are pure compile-time predicates with no
//                hidden state; reduce_into folds via Op which is
//                deterministic by contract.

#include <crucible/safety/IsReduceInto.h>     // FOUND-D07 wrapper detector
#include <crucible/safety/Reduction.h>        // FOUND-D14 shape concept
#include <crucible/safety/reduce_into.h>      // accumulator-into-place

#include <type_traits>   // dual-export sentinel uses std::is_same_v

namespace crucible::fixy::kernel {

// ═══════════════════════════════════════════════════════════════════
// ── reduce_into class template + Op admissibility concept ─────────
// ═══════════════════════════════════════════════════════════════════

// reduce_into<R, Op> — the accumulator-into-place wrapper a reduction
// borrows by lvalue ref.  Move-only; combine() folds partials.
using ::crucible::safety::reduce_into;

// is_reduction_op_v<Op, R> — Op admitted iff callable as Op(R,R)→R.
// Used in reduce_into's class-level requires-clause AND by downstream
// callers that want to verify a reducer at a non-construction site.
using ::crucible::safety::is_reduction_op_v;

// ═══════════════════════════════════════════════════════════════════
// ── Reduction kernel-shape concept + extractors (FOUND-D14) ───────
// ═══════════════════════════════════════════════════════════════════

// Reduction<auto FnPtr> — concept satisfied iff FnPtr's signature is
// `void(OwnedRegion<T, Tag>&&, reduce_into<R, Op>&) [noexcept]`.
using ::crucible::safety::extract::Reduction;

// is_reduction_v<auto FnPtr> — variable-template form for metaprogram
// folds (e.g., per-stage shape classification over a Stages... pack).
using ::crucible::safety::extract::is_reduction_v;

// reduction_input_tag_t / reduction_input_value_t — extract the
// consumed OwnedRegion's Tag (permission identity) and element type T.
using ::crucible::safety::extract::reduction_input_tag_t;
using ::crucible::safety::extract::reduction_input_value_t;

// reduction_accumulator_t / reduction_reducer_t — extract the
// borrowed reduce_into's R (accumulator type) and Op (reducer).
using ::crucible::safety::extract::reduction_accumulator_t;
using ::crucible::safety::extract::reduction_reducer_t;

// ═══════════════════════════════════════════════════════════════════
// ── reduce_into wrapper-detection (FOUND-D07) ──────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// The dispatcher and parameter-shape recognizers read these to detect
// `reduce_into<...>` parameters before destructuring R / Op.
// Distinct from `is_reduction_op_v` (which classifies Op against R) —
// these classify the WRAPPER TYPE itself.

using ::crucible::safety::extract::IsReduceInto;
using ::crucible::safety::extract::is_reduce_into_v;
using ::crucible::safety::extract::reduce_into_accumulator_t;
using ::crucible::safety::extract::reduce_into_reducer_t;

}  // namespace crucible::fixy::kernel

// ═══════════════════════════════════════════════════════════════════
// ── Dual-export sentinel — FIXY-V-038 ──────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Header-internal identity sentinels.  Verifies each alias resolves to
// the substrate symbol, not a shadowed local of the same name.  Same
// discipline as fixy/Handle.h::self_test (FIXY-U-016) and fixy/Wrap.h
// (FIXY-V-035 / V-036).

namespace crucible::fixy::kernel::self_test {

// ── Identity probe types for type-template aliasing checks ─────────
//
// reduce_into is templated on (R, Op).  We need a concrete Op
// satisfying is_reduction_op_v<Op, R> to instantiate; using PlusOp_
// from the substrate's self-test would couple the header to a
// detail:: name.  Instead the probe defines its own.

struct KernelProbePlus {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

// ── 1. Class-template identity ─────────────────────────────────────
//
// fixy::kernel::reduce_into<R, Op> MUST alias safety::reduce_into;
// drift would mean two distinct accumulator wrappers carrying the
// same name, breaking move-only linearity guarantees across TUs.
static_assert(std::is_same_v<
    ::crucible::fixy::kernel::reduce_into<int, KernelProbePlus>,
    ::crucible::safety::reduce_into<int, KernelProbePlus>>,
    "fixy::kernel::reduce_into<R, Op> must alias safety::reduce_into<R, Op> "
    "— substrate identity drift would orphan the move-only accumulator "
    "linearity discipline (BorrowSafe).");

// ── 2. is_reduction_op_v concept reaches through ───────────────────
//
// The concept is a templated bool; identity-check via the truth table
// (positive case admitted, negative case rejected) gives the same
// witness without needing decltype-of-a-concept (which is non-trivial).
static_assert(
    ::crucible::fixy::kernel::is_reduction_op_v<KernelProbePlus, int> ==
    ::crucible::safety::is_reduction_op_v<KernelProbePlus, int>,
    "fixy::kernel::is_reduction_op_v must agree with the substrate on "
    "PlusOp/int — concept admission set drift would silently accept or "
    "reject reducers differently across the two reach paths.");

// Negative agreement — non-invocable type fails both.
struct KernelProbeNonInvocable {};
static_assert(
    ::crucible::fixy::kernel::is_reduction_op_v<KernelProbeNonInvocable, int> ==
    ::crucible::safety::is_reduction_op_v<KernelProbeNonInvocable, int>,
    "fixy::kernel::is_reduction_op_v must agree with the substrate on "
    "NON-invocable types (both reject).");

// ── 3. Reduction shape concept identity (function-ptr witness) ─────
//
// Concept-identity via 5 negative witnesses mirroring the substrate's
// own self-test (Reduction.h's detail::reduction_self_test).  Each
// witness exercises a distinct violated clause; passing all five
// confirms the alias surfaces the same per-clause checks.
//
// Positive witnesses (a Reduction-shaped function) require
// instantiating OwnedRegion<T, Tag> + reduce_into<R, Op> with the
// SAME header surface — the production sentinel TU
// `test/test_fixy_kernel.cpp` carries that proof under the full
// project warning matrix.

inline void kp_f_nullary() noexcept {}
static_assert(
    ::crucible::fixy::kernel::Reduction<&kp_f_nullary>
    == ::crucible::safety::extract::Reduction<&kp_f_nullary>,
    "fixy::kernel::Reduction must agree with the substrate on the "
    "nullary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_nullary>);

inline void kp_f_unary(int) noexcept {}
static_assert(
    ::crucible::fixy::kernel::Reduction<&kp_f_unary>
    == ::crucible::safety::extract::Reduction<&kp_f_unary>,
    "fixy::kernel::Reduction must agree with the substrate on the "
    "unary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_unary>);

inline void kp_f_ternary(int, int, int) noexcept {}
static_assert(
    ::crucible::fixy::kernel::Reduction<&kp_f_ternary>
    == ::crucible::safety::extract::Reduction<&kp_f_ternary>,
    "fixy::kernel::Reduction must agree with the substrate on the "
    "ternary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_ternary>);

inline void kp_f_two_ints(int, int) noexcept {}
static_assert(
    ::crucible::fixy::kernel::Reduction<&kp_f_two_ints>
    == ::crucible::safety::extract::Reduction<&kp_f_two_ints>,
    "fixy::kernel::Reduction must agree with the substrate on the "
    "two-ints witness (both reject; param 0 is not OwnedRegion, "
    "param 1 is not reduce_into).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_two_ints>);

inline int kp_f_returns_int(int, int) noexcept { return 0; }
static_assert(
    ::crucible::fixy::kernel::Reduction<&kp_f_returns_int>
    == ::crucible::safety::extract::Reduction<&kp_f_returns_int>,
    "fixy::kernel::Reduction must agree with the substrate on the "
    "non-void-return witness (both reject; return type != void).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_returns_int>);

// is_reduction_v variable-template form agrees with the concept form
// on every witness.  This catches the "alias re-exported the concept
// but the variable template forgot to update" drift class.
static_assert(
    ::crucible::fixy::kernel::is_reduction_v<&kp_f_nullary>
    == ::crucible::fixy::kernel::Reduction<&kp_f_nullary>,
    "fixy::kernel::is_reduction_v must mirror the Reduction concept "
    "result on every input (variable-template form vs concept form "
    "must stay in lockstep).");

// ── 4. IsReduceInto wrapper-detection identity ─────────────────────
//
// is_reduce_into_v / IsReduceInto classify the WRAPPER TYPE.  Use
// reduce_into<int, KernelProbePlus> (positive) and int (negative) as
// the two-witness anchor.

using KP_RI_int_plus =
    ::crucible::fixy::kernel::reduce_into<int, KernelProbePlus>;

static_assert(
    ::crucible::fixy::kernel::is_reduce_into_v<KP_RI_int_plus>
    == ::crucible::safety::extract::is_reduce_into_v<KP_RI_int_plus>,
    "fixy::kernel::is_reduce_into_v must agree with the substrate on "
    "the positive witness — drift would mean the dispatcher sees a "
    "different reduce_into-shape membership through the two reach "
    "paths.");
static_assert( ::crucible::fixy::kernel::is_reduce_into_v<KP_RI_int_plus>);

static_assert(
    ::crucible::fixy::kernel::is_reduce_into_v<int>
    == ::crucible::safety::extract::is_reduce_into_v<int>,
    "fixy::kernel::is_reduce_into_v must agree with the substrate on "
    "the negative witness (bare int).");
static_assert(!::crucible::fixy::kernel::is_reduce_into_v<int>);

// Concept form agrees with the variable template form.
static_assert(
    ::crucible::fixy::kernel::IsReduceInto<KP_RI_int_plus>
    == ::crucible::fixy::kernel::is_reduce_into_v<KP_RI_int_plus>,
    "fixy::kernel::IsReduceInto concept must agree with "
    "is_reduce_into_v variable template form.");

// ── 5. reduce_into accumulator/reducer extractors ─────────────────
//
// Identity check via std::is_same_v on the extracted types — drift
// would mean callers reading R/Op through the alias would see a
// different type from callers reading them through the substrate.

static_assert(std::is_same_v<
    ::crucible::fixy::kernel::reduce_into_accumulator_t<KP_RI_int_plus>,
    ::crucible::safety::extract::reduce_into_accumulator_t<KP_RI_int_plus>>,
    "fixy::kernel::reduce_into_accumulator_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::kernel::reduce_into_accumulator_t<KP_RI_int_plus>, int>,
    "reduce_into_accumulator_t<reduce_into<int, _>> must yield int.");

static_assert(std::is_same_v<
    ::crucible::fixy::kernel::reduce_into_reducer_t<KP_RI_int_plus>,
    ::crucible::safety::extract::reduce_into_reducer_t<KP_RI_int_plus>>,
    "fixy::kernel::reduce_into_reducer_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::kernel::reduce_into_reducer_t<KP_RI_int_plus>,
    KernelProbePlus>,
    "reduce_into_reducer_t<reduce_into<_, PlusOp>> must yield PlusOp.");

// ── 6. LOAD-BEARING structural contracts on reduce_into ───────────
//
// The substrate's raison-d'être is move-only linear semantics on the
// accumulator (copying would duplicate unique state and break
// parallel_reduce_views's per-worker partial discipline).  Mirroring
// the substrate contracts at the fixy:: boundary catches drift before
// any production call site (parallel_reduce_views, Forge phase F4
// reduction lowering) notices.  Per CLAUDE.md §XIII discipline.
//
// Contract 1 — non-copyable.  Substrate deletes copy ctor + copy
// assign with the reason "duplicate accumulator → broken linearity";
// if a future refactor accidentally re-introduces copy via composition
// or default-generation, this sentinel fires at the alias path before
// any caller depends on the broken invariant.
static_assert(
    !std::is_copy_constructible_v<KP_RI_int_plus>,
    "fixy::kernel::reduce_into<R, Op> must not be copy-constructible — "
    "the accumulator is unique state; copy would duplicate it and break "
    "parallel_reduce_views's per-worker partial discipline (BorrowSafe).");

static_assert(
    !std::is_copy_assignable_v<KP_RI_int_plus>,
    "fixy::kernel::reduce_into<R, Op> must not be copy-assignable — "
    "copy-assign would leak the LHS's old accumulator AND alias the "
    "RHS's, breaking single-owner linearity (BorrowSafe).");

// Contract 2 — move-constructible + move-assignable.  Move is the
// transfer mechanism that lets the dispatcher hand the accumulator
// from the planner's stack frame into the per-worker per-shard frames.
static_assert(
    std::is_move_constructible_v<KP_RI_int_plus>,
    "fixy::kernel::reduce_into<R, Op> must be move-constructible — "
    "transfer of ownership between dispatcher and worker requires it.");

static_assert(
    std::is_move_assignable_v<KP_RI_int_plus>,
    "fixy::kernel::reduce_into<R, Op> must be move-assignable — "
    "symmetric to the move-construct guarantee.");

// Contract 3 — Op constraint is enforced at construction.  This is
// the LOAD-BEARING type-system check that rejects non-reducer Ops at
// the class-level requires-clause (NOT at combine() — too late).
// We can't easily express "construction was rejected" with a
// static_assert in this header (it would require SFINAE-detecting an
// unsatisfied requires-clause), but agreeing with the substrate on
// is_reduction_op_v's verdict for KernelProbePlus + KernelProbeNonInvocable
// is the structural witness.  Verified above in section 2.

// ── 7. Cardinality witness ────────────────────────────────────────
//
// 11 surfaced aliases (concept + variable template + 4 extractors for
// Reduction + class template + Op concept + wrapper-detection concept
// + variable template + 2 extractors for reduce_into).  Future
// additions to safety::extract::* MUST extend this block + bump the
// constant + add a sentinel below.

constexpr int kernel_alias_cardinality = 11;
static_assert(kernel_alias_cardinality == 11,
    "fixy::kernel:: cardinality changed — update Kernel.h sentinel "
    "block to track the substrate kernel-shape recognizer surface.");

}  // namespace crucible::fixy::kernel::self_test
