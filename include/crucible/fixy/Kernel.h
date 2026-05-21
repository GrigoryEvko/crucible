#pragma once

// ── crucible::fixy::kernel — kernel-shape recognizer surface ───────
//
// Surfaces the function-pointer-shape recognizers from
// `include/crucible/safety/{BinaryTransform,CanonicalShape,Fusion,
// IsReduceInto,Reduction,UnaryTransform,reduce_into}.h` under
// `fixy::kernel::`.  Per misc/27_04_2026.md §3.3 +
// misc/28_04_2026_effects.md §6.1 §10 + FIXY-V-038 (Reduction family)
// + FIXY-V-039 (Unary/Binary/CanonicalShape/Fusion): closes the
// umbrella-reach gap where dispatcher / Forge-phase code wanting to
// recognize "this function is a Reduction" had to descend into
// `crucible::safety::extract::` (the recognizer's actual namespace,
// except Fusion which lives directly in `crucible::safety::`).
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
//   safety::extract::UnaryTransform<auto FnPtr>
//                                    — FOUND-D12 arity-1 OwnedRegion shape
//   safety::extract::is_unary_transform_v
//                                    — variable-template form
//   safety::extract::is_in_place_unary_transform_v
//                                    — void-return refinement
//   safety::extract::unary_transform_{input_tag,input_value,output_tag}_t
//                                    — 3 extractors
//   safety::extract::BinaryTransform<auto FnPtr>
//                                    — FOUND-D13 arity-2 OwnedRegion shape
//   safety::extract::is_binary_transform_v
//                                    — variable-template form
//   safety::extract::is_in_place_binary_transform_v
//                                    — void-return refinement
//   safety::extract::binary_transform_{lhs_tag,rhs_tag,lhs_value,
//                                       rhs_value,output_tag}_t
//                                    — 5 extractors
//   safety::extract::binary_transform_has_same_tag_v
//                                    — lhs/rhs tag-equality predicate
//   safety::extract::{CanonicalShape,NonCanonical}<auto FnPtr>
//                                    — FOUND-D20 umbrella + complement
//   safety::extract::is_{canonical_shape,non_canonical}_v
//                                    — variable-template forms
//   safety::extract::CanonicalShapeKind
//                                    — 9-element enum class
//   safety::extract::canonical_shape_kind_v
//                                    — dispatch lookup
//   safety::extract::canonical_shape_name / canonical_shape_name_of_v
//                                    — string_view diagnostics
//   safety::can_fuse_v / safety::IsFusable / safety::fuse
//                                    — FOUND-F06/F07 fusion composability
//                                      (Fusion ships in `safety::`,
//                                      NOT `safety::extract::`)
//
// ── Role ───────────────────────────────────────────────────────────
//
// `fixy::kernel::` is the third top-level fixy namespace family.
// `fixy::handle::` covers RAII resource handles, `fixy::wrap::` covers
// value-wrappers; `fixy::kernel::` covers KERNEL-SHAPE RECOGNIZERS —
// function-pointer-shape concepts and their extractors that the
// dispatcher reads to route each function to its canonical lowering
// (UnaryTransform → parallel_map; Reduction → parallel_reduce_views;
// BinaryTransform → parallel_combine; etc.).  V-038 shipped the
// Reduction family; V-039 shipped UnaryTransform / BinaryTransform /
// CanonicalShape umbrella / Fusion composability.  Future re-exports
// (V-043) will add the remaining PipelineStage / ConsumerEndpoint /
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

#include <crucible/safety/BinaryTransform.h>   // FOUND-D13 arity-2 shape
#include <crucible/safety/CanonicalShape.h>    // FOUND-D20 umbrella + dispatch
#include <crucible/safety/Fusion.h>            // FOUND-F06/F07 fusion composability
#include <crucible/safety/IsReduceInto.h>      // FOUND-D07 wrapper detector
#include <crucible/safety/Reduction.h>         // FOUND-D14 shape concept
#include <crucible/safety/UnaryTransform.h>    // FOUND-D12 arity-1 shape
#include <crucible/safety/reduce_into.h>       // accumulator-into-place

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

// ═══════════════════════════════════════════════════════════════════
// ── UnaryTransform kernel-shape concept + extractors (FOUND-D12) ──
// ═══════════════════════════════════════════════════════════════════
//
// arity == 1, parameter 0 = OwnedRegion<T, Tag>&& (non-const rvalue
// ref), return type = void (in-place) OR OwnedRegion (out-of-place).
// Lowering target: parallel_for_views<N> over the consumed region.

using ::crucible::safety::extract::UnaryTransform;
using ::crucible::safety::extract::is_unary_transform_v;
using ::crucible::safety::extract::is_in_place_unary_transform_v;
using ::crucible::safety::extract::unary_transform_input_tag_t;
using ::crucible::safety::extract::unary_transform_input_value_t;
using ::crucible::safety::extract::unary_transform_output_tag_t;

// ═══════════════════════════════════════════════════════════════════
// ── BinaryTransform kernel-shape concept + extractors (FOUND-D13) ─
// ═══════════════════════════════════════════════════════════════════
//
// arity == 2, both parameters = OwnedRegion<T, Tag>&& (non-const
// rvalue refs), return type = void (in-place against lhs) OR
// OwnedRegion (out-of-place).  Lowering target: parallel_combine.

using ::crucible::safety::extract::BinaryTransform;
using ::crucible::safety::extract::is_binary_transform_v;
using ::crucible::safety::extract::is_in_place_binary_transform_v;
using ::crucible::safety::extract::binary_transform_lhs_tag_t;
using ::crucible::safety::extract::binary_transform_rhs_tag_t;
using ::crucible::safety::extract::binary_transform_lhs_value_t;
using ::crucible::safety::extract::binary_transform_rhs_value_t;
using ::crucible::safety::extract::binary_transform_output_tag_t;
using ::crucible::safety::extract::binary_transform_has_same_tag_v;

// ═══════════════════════════════════════════════════════════════════
// ── CanonicalShape umbrella + dispatch (FOUND-D20) ────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Umbrella over the 8 per-shape recognizers (UnaryTransform,
// BinaryTransform, Reduction, ProducerEndpoint, ConsumerEndpoint,
// SwmrWriter, SwmrReader, PipelineStage).  CanonicalShape admits a
// function iff at least one shape matches; NonCanonical is the §3.8
// catch-all.  CanonicalShapeKind enumerates the 9 outcomes (8 shapes
// + NonCanonical fallback).  Shapes are MUTUALLY EXCLUSIVE — at most
// one per-shape predicate is true per FnPtr (CanonicalShape.h §5.6).

using ::crucible::safety::extract::CanonicalShape;
using ::crucible::safety::extract::NonCanonical;
using ::crucible::safety::extract::is_canonical_shape_v;
using ::crucible::safety::extract::is_non_canonical_v;
using ::crucible::safety::extract::CanonicalShapeKind;
using ::crucible::safety::extract::canonical_shape_kind_v;
using ::crucible::safety::extract::canonical_shape_name;
using ::crucible::safety::extract::canonical_shape_name_of_v;

// ═══════════════════════════════════════════════════════════════════
// ── Fusion composability (FOUND-F06 + F07) ─────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// can_fuse_v / IsFusable: structural predicate over a pair of pure
// noexcept callables (Fn1's return matches Fn2's single param after
// cv-ref stripping).  fuse(): stateless lambda generator yielding the
// composed `[](auto x) noexcept { return Fn2(Fn1(x)); }`.  Substrate
// ships in `crucible::safety::` (NOT `extract::`) so the using-decls
// here cite the non-extract namespace.

using ::crucible::safety::can_fuse_v;
using ::crucible::safety::IsFusable;
using ::crucible::safety::fuse;

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

// ── 7. UnaryTransform identity + agreement (V-039) ────────────────
//
// UnaryTransform concept + 5 supporting symbols (variable templates +
// extractors).  Header self-test covers only negatives that don't
// require instantiating OwnedRegion (which would expand the include
// surface unnecessarily); the production sentinel TU
// `test/test_fixy_kernel.cpp` positively witnesses the shape against
// OwnedRegion-typed fixtures under the project warning matrix.

static_assert(
    ::crucible::fixy::kernel::UnaryTransform<&kp_f_nullary>
    == ::crucible::safety::extract::UnaryTransform<&kp_f_nullary>,
    "fixy::kernel::UnaryTransform must agree with the substrate on "
    "the nullary witness (both reject; arity != 1).");
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_nullary>);

static_assert(
    ::crucible::fixy::kernel::UnaryTransform<&kp_f_unary>
    == ::crucible::safety::extract::UnaryTransform<&kp_f_unary>,
    "fixy::kernel::UnaryTransform must agree with the substrate on "
    "the int-unary witness (both reject; parameter 0 is not an "
    "OwnedRegion rvalue ref).");
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_unary>);

static_assert(
    ::crucible::fixy::kernel::UnaryTransform<&kp_f_two_ints>
    == ::crucible::safety::extract::UnaryTransform<&kp_f_two_ints>,
    "fixy::kernel::UnaryTransform must agree with the substrate on "
    "the two-ints witness (both reject; arity != 1).");
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_two_ints>);

// Variable-template / concept lockstep — catches the "alias re-
// exported the concept but the variable template forgot to update"
// drift class (same shape as the section-3 is_reduction_v check).
static_assert(
    ::crucible::fixy::kernel::is_unary_transform_v<&kp_f_nullary>
    == ::crucible::fixy::kernel::UnaryTransform<&kp_f_nullary>,
    "fixy::kernel::is_unary_transform_v must mirror the UnaryTransform "
    "concept on every input (variable-template vs concept lockstep).");

// ── 8. BinaryTransform identity + agreement (V-039) ───────────────
//
// Same shape as section 7 but for BinaryTransform (FOUND-D13).
// arity == 2 with BOTH params being OwnedRegion&& — distinct from
// Reduction (param 1 is reduce_into&) and UnaryTransform (arity == 1).

static_assert(
    ::crucible::fixy::kernel::BinaryTransform<&kp_f_nullary>
    == ::crucible::safety::extract::BinaryTransform<&kp_f_nullary>,
    "fixy::kernel::BinaryTransform must agree with the substrate on "
    "the nullary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_f_nullary>);

static_assert(
    ::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>
    == ::crucible::safety::extract::BinaryTransform<&kp_f_two_ints>,
    "fixy::kernel::BinaryTransform must agree with the substrate on "
    "the two-ints witness (both reject; parameters are not "
    "OwnedRegion rvalue refs).");
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>);

static_assert(
    ::crucible::fixy::kernel::is_binary_transform_v<&kp_f_two_ints>
    == ::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>,
    "fixy::kernel::is_binary_transform_v must mirror the "
    "BinaryTransform concept (variable-template / concept lockstep).");

// ── 9. CanonicalShape umbrella + enum identity (V-039) ────────────
//
// CRITICAL invariant: the `enum class CanonicalShapeKind` must be
// the SAME TYPE when reached through fixy:: or safety::extract::.  A
// using-declaration preserves type identity; if the substrate ever
// switched to redeclaring the enum, every cross-namespace enumerator
// comparison would silently change from "true" (same type) to
// "compile error" (incompatible enums).  This sentinel catches that
// drift class at the alias path.

static_assert(
    std::is_same_v<
        ::crucible::fixy::kernel::CanonicalShapeKind,
        ::crucible::safety::extract::CanonicalShapeKind>,
    "fixy::kernel::CanonicalShapeKind must BE the substrate enum, "
    "not a redeclaration — identity drift here would silently break "
    "every enumerator comparison across the two reach paths.");

// All 9 enumerators reachable through the alias.  Given enum
// identity above, these comparisons are tautologically true; the
// witnesses act as defense-in-depth + structurally pin the 9 expected
// shape labels so future appends to CanonicalShapeKind ripple here.
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::NonCanonical
           == ::crucible::safety::extract::CanonicalShapeKind::NonCanonical);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::UnaryTransform
           == ::crucible::safety::extract::CanonicalShapeKind::UnaryTransform);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::BinaryTransform
           == ::crucible::safety::extract::CanonicalShapeKind::BinaryTransform);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::Reduction
           == ::crucible::safety::extract::CanonicalShapeKind::Reduction);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::ProducerEndpoint
           == ::crucible::safety::extract::CanonicalShapeKind::ProducerEndpoint);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::ConsumerEndpoint
           == ::crucible::safety::extract::CanonicalShapeKind::ConsumerEndpoint);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::SwmrWriter
           == ::crucible::safety::extract::CanonicalShapeKind::SwmrWriter);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::SwmrReader
           == ::crucible::safety::extract::CanonicalShapeKind::SwmrReader);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::PipelineStage
           == ::crucible::safety::extract::CanonicalShapeKind::PipelineStage);

// CanonicalShape umbrella + NonCanonical complement agree with the
// substrate on the function-pointer witnesses defined in section 3.
// kp_f_two_ints (void(int,int)) matches NONE of the 8 canonical
// shapes → CanonicalShape = false, NonCanonical = true.

static_assert(
    ::crucible::fixy::kernel::CanonicalShape<&kp_f_two_ints>
    == ::crucible::safety::extract::CanonicalShape<&kp_f_two_ints>);
static_assert(!::crucible::fixy::kernel::CanonicalShape<&kp_f_two_ints>);

static_assert(
    ::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>
    == ::crucible::safety::extract::NonCanonical<&kp_f_two_ints>);
static_assert(::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>);

// Variable-template forms agree with their concept forms.
static_assert(
    ::crucible::fixy::kernel::is_canonical_shape_v<&kp_f_two_ints>
    == ::crucible::fixy::kernel::CanonicalShape<&kp_f_two_ints>);
static_assert(
    ::crucible::fixy::kernel::is_non_canonical_v<&kp_f_two_ints>
    == ::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>);

// Dispatch — canonical_shape_kind_v routes kp_f_two_ints to
// NonCanonical (none of the 8 shapes match).
static_assert(
    ::crucible::fixy::kernel::canonical_shape_kind_v<&kp_f_two_ints>
    == ::crucible::fixy::kernel::CanonicalShapeKind::NonCanonical,
    "kp_f_two_ints (void(int,int)) matches no canonical shape — "
    "dispatch must resolve to NonCanonical.");

// canonical_shape_name lookup — string_view round-trip across the
// 4 most-critical shapes (Reduction-shape is V-038, Unary/Binary are
// V-039, NonCanonical is the fallback).
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name(
        ::crucible::fixy::kernel::CanonicalShapeKind::NonCanonical)
    == std::string_view{"NonCanonical"});
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name(
        ::crucible::fixy::kernel::CanonicalShapeKind::Reduction)
    == std::string_view{"Reduction"});
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name(
        ::crucible::fixy::kernel::CanonicalShapeKind::UnaryTransform)
    == std::string_view{"UnaryTransform"});
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name(
        ::crucible::fixy::kernel::CanonicalShapeKind::BinaryTransform)
    == std::string_view{"BinaryTransform"});

// canonical_shape_name_of_v — pre-dispatched name variable for the
// non-canonical witness.  Composition of canonical_shape_kind_v +
// canonical_shape_name verified through the alias.
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name_of_v<&kp_f_two_ints>
    == std::string_view{"NonCanonical"});

// ── 10. Fusion composability (V-039) ──────────────────────────────
//
// Fusion's substrate lives in `crucible::safety::` (NOT `extract::`).
// Two pure noexcept int(int) functions form the canonical positive
// witness — composability does NOT depend on OwnedRegion / reduce_into
// so the positive case ships at the header level (unlike the other
// shape recognizers in sections 7-9 whose positives ship in the TU).

inline int kp_p_double(int x) noexcept { return x * 2; }
inline int kp_p_inc(int x)    noexcept { return x + 1; }

// Positive: matching types (int → int → int), both pure + noexcept,
// arity 1 — all five F06 clauses hold.
static_assert(
    ::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_p_inc>
    == ::crucible::safety::can_fuse_v<&kp_p_double, &kp_p_inc>,
    "fixy::kernel::can_fuse_v must agree with the substrate on the "
    "canonical positive witness (matching int(int) types).");
static_assert(::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_p_inc>);

// IsFusable concept form agrees with can_fuse_v variable-template form.
static_assert(::crucible::fixy::kernel::IsFusable<&kp_p_double, &kp_p_inc>);
static_assert(
    ::crucible::fixy::kernel::IsFusable<&kp_p_double, &kp_p_inc>
    == ::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_p_inc>);

// Negative: arity mismatch — kp_f_nullary takes 0 args, can't pipe
// kp_p_double's int return into it (arity_v<Fn2> != 1).
static_assert(
    !::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_f_nullary>);

// Negative: arity mismatch the other direction — kp_f_two_ints takes
// (int, int), Fn2 arity != 1.
static_assert(
    !::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_f_two_ints>);

// fuse() — LOAD-BEARING runtime contract.  The returned stateless
// lambda must compute Fn2(Fn1(x)) AND propagate noexcept.  Compile-
// time evaluation proves both at the alias path; if `fuse` ever drifts
// from `[](auto x){ return Fn2(Fn1(x)); }` to something else (e.g.
// reversed order, captured state), these sentinels fire.
constexpr auto kp_fused = ::crucible::fixy::kernel::fuse<
    &kp_p_double, &kp_p_inc>();
static_assert(kp_fused(7) == 15,
    "fuse<&double, &inc>()(7) must compute inc(double(7)) = 14+1 = 15.");
static_assert(kp_fused(0) == 1,
    "fuse<&double, &inc>()(0) must compute inc(0*2) = 1.");
static_assert(noexcept(kp_fused(7)),
    "fuse() must produce a noexcept callable when both inputs are "
    "noexcept (substrate inherits the noexcept-ness via the lambda's "
    "noexcept(noexcept(Fn2(Fn1(x)))) computed exception specifier).");

// Empty + trivially copyable — load-bearing for ICF and the F08 bench
// pair-0 "indistinguishable" claim.  Mirrors Fusion.h section 249-272.
static_assert(std::is_empty_v<decltype(kp_fused)>,
    "fuse() must produce a stateless empty closure — non-empty would "
    "break ICF folding under the linker's identical-code-collapse pass "
    "(and silently bloat the fused-callable layout).");
static_assert(sizeof(decltype(kp_fused)) == 1,
    "Empty closure has the standard-mandated 1-byte size — larger "
    "implies hidden capture state contradicting the F07 promise.");

// ── 11. Cross-shape exclusivity (V-039) ───────────────────────────
//
// Mutual-exclusivity guarantee (CanonicalShape.h §5.6): a function
// satisfies AT MOST ONE shape predicate.  Header-level witnesses use
// kp_f_two_ints (NonCanonical) + kp_p_double (NonCanonical) to prove
// the per-shape recognizers consistently REJECT non-region parameter
// shapes across the whole D12-D14 axis.  Positive cross-exclusion
// witnesses (a UnaryTransform-shaped function that is NOT a
// BinaryTransform, etc.) require OwnedRegion + reduce_into
// instantiation and live in the TU (section 10).

// kp_f_two_ints matches NO canonical shape.
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_two_ints>);
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>);
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_two_ints>);
static_assert( ::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>);

// kp_p_double matches NO canonical shape (arity 1, parameter is int
// — not an OwnedRegion rvalue ref).  Same conclusion across the
// per-shape recognizers AS for kp_f_two_ints (different rejection
// path: kp_f_two_ints fails on arity; kp_p_double fails on param
// type).
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_p_double>);
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_p_double>);
static_assert(!::crucible::fixy::kernel::Reduction<&kp_p_double>);
static_assert( ::crucible::fixy::kernel::NonCanonical<&kp_p_double>);

// ── 12. Cardinality witness ───────────────────────────────────────
//
// 38 surfaced using-declarations across 7 substrate sub-families:
//
//   reduce_into family (2) — V-038:
//     (1) reduce_into          — class template
//     (2) is_reduction_op_v    — Op admissibility concept
//
//   Reduction shape family (6) — V-038:
//     (3) Reduction            — kernel-shape concept (FOUND-D14)
//     (4) is_reduction_v       — variable-template form of (3)
//     (5..8) reduction_input_tag_t / reduction_input_value_t
//                              / reduction_accumulator_t
//                              / reduction_reducer_t   — 4 extractors
//
//   reduce_into wrapper-detection family (4) — V-038:
//     (9)  IsReduceInto            — wrapper-detection concept (FOUND-D07)
//     (10) is_reduce_into_v        — variable-template form of (9)
//     (11) reduce_into_accumulator_t — R from reduce_into<R, _>
//     (12) reduce_into_reducer_t     — Op from reduce_into<_, Op>
//
//   UnaryTransform family (6) — V-039:
//     (13) UnaryTransform                       — kernel-shape concept (FOUND-D12)
//     (14) is_unary_transform_v                 — variable-template form
//     (15) is_in_place_unary_transform_v        — void-return refinement
//     (16..18) unary_transform_input_tag_t /
//              unary_transform_input_value_t /
//              unary_transform_output_tag_t     — 3 extractors
//
//   BinaryTransform family (9) — V-039:
//     (19) BinaryTransform                      — kernel-shape concept (FOUND-D13)
//     (20) is_binary_transform_v                — variable-template form
//     (21) is_in_place_binary_transform_v       — void-return refinement
//     (22..25) binary_transform_lhs_tag_t / binary_transform_rhs_tag_t /
//              binary_transform_lhs_value_t / binary_transform_rhs_value_t
//                                              — 4 extractors
//     (26) binary_transform_output_tag_t        — output extractor
//     (27) binary_transform_has_same_tag_v      — lhs/rhs tag-equality
//
//   CanonicalShape umbrella (8) — V-039:
//     (28) CanonicalShape                       — umbrella concept (FOUND-D20)
//     (29) NonCanonical                         — complement concept
//     (30) is_canonical_shape_v                 — variable-template form
//     (31) is_non_canonical_v                   — complement variable-template
//     (32) CanonicalShapeKind                   — 9-element enum class
//     (33) canonical_shape_kind_v               — dispatch variable
//     (34) canonical_shape_name                 — string_view lookup
//     (35) canonical_shape_name_of_v            — pre-dispatched name
//
//   Fusion family (3) — V-039:
//     (36) can_fuse_v                           — composability (FOUND-F06)
//     (37) IsFusable                            — concept form
//     (38) fuse                                 — generator (FOUND-F07)
//
// FIXY-V-038-audit: bumped 11 → 12 after the cardinality off-by-one
// was caught by the post-ship audit (the original count enumerated
// 12 items textually but stated 11; the floor sentinel in
// test_fixy_kernel.cpp was at >= 11, hiding the drift).
// FIXY-V-039: bumped 12 → 38 adding UnaryTransform family (6) +
// BinaryTransform family (9) + CanonicalShape umbrella (8) + Fusion
// family (3).
//
// Future additions to safety::extract::* / safety::* MUST extend
// this block + bump the constant + add a sentinel above.

constexpr int kernel_alias_cardinality = 38;
static_assert(kernel_alias_cardinality == 38,
    "fixy::kernel:: cardinality changed — update Kernel.h sentinel "
    "block to track the substrate kernel-shape recognizer surface.");

}  // namespace crucible::fixy::kernel::self_test
