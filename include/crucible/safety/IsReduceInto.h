#pragma once

// ── crucible::safety::extract::is_reduce_into_v ─────────────────────
//
// FOUND-D07 of 28_04_2026_effects.md §10 + 27_04_2026.md §5.5.
// Wrapper-detection predicate for `safety::reduce_into<R, Op>`.  Part
// of the FOUND-D series of trait detectors that the dispatcher reads
// to recognize Permissioned/Reduction-shaped function parameters and
// route them to the appropriate per-shape lowering.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_reduce_into_v<T>      Variable template: true iff T (after
//                            cv-ref stripping) is a specialization of
//                            reduce_into<R, Op> for some R and Op.
//   IsReduceInto<T>          Concept form for `requires`-clauses.
//   reduce_into_accumulator_t<T>
//                            Alias to R when T is a reduce_into;
//                            ill-formed otherwise.
//   reduce_into_reducer_t<T> Alias to Op when T is a reduce_into;
//                            ill-formed otherwise.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Canonical primary-false-template + partial-specialization-true
// pattern, mirroring is_owned_region_v / is_permission_v.
// `std::remove_cvref_t<T>` strips reference categories so the
// dispatcher can write the predicate against the parameter type
// directly without manual decay.
//
// ── Downstream use (FOUND-D14) ──────────────────────────────────────
//
// The Reduction concept (FOUND-D14) uses this trait to recognize the
// canonical reduction-shape signature:
//
//   void f(OwnedRegion<T, Tag>&&, reduce_into<R, Op>&);
//
// That is: arity == 2, param 0 is a non-const rvalue-ref OwnedRegion
// (consumed input), param 1 is a non-const lvalue-ref reduce_into
// (borrowed accumulator).  This trait answers the param-1 detection
// half of that concept.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — partial specialization is the only true case;
//              everything else is false.  No silent conversions.
//   DetSafe — same T → same value, deterministically; no hidden
//              state.

#include <crucible/safety/reduce_into.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: primary + partial specialization ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename T>
struct is_reduce_into_impl : std::false_type {
    using accumulator_type = void;
    using reducer_type     = void;
};

template <typename R, typename Op>
struct is_reduce_into_impl<::crucible::safety::reduce_into<R, Op>>
    : std::true_type
{
    using accumulator_type = R;
    using reducer_type     = Op;
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_reduce_into_v =
    detail::is_reduce_into_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsReduceInto = is_reduce_into_v<T>;

// reduce_into_accumulator_t / reduce_into_reducer_t are constrained on
// is_reduce_into_v to produce a clean compile error rather than `void`
// for non-reduce_into arguments.

template <typename T>
    requires is_reduce_into_v<T>
using reduce_into_accumulator_t =
    typename detail::is_reduce_into_impl<
        std::remove_cvref_t<T>>::accumulator_type;

template <typename T>
    requires is_reduce_into_v<T>
using reduce_into_reducer_t =
    typename detail::is_reduce_into_impl<
        std::remove_cvref_t<T>>::reducer_type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Adversarial cases verified at compile time.  Every claim is
// duplicated by the sentinel TU under the project's full warning
// matrix.

namespace detail::is_reduce_into_self_test {

// Test reducers — simple stateless function objects satisfying
// is_reduction_op_v<Op, R>.
struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

struct DoublePlusOp {
    constexpr double operator()(double const& a, double const& b) const noexcept {
        return a + b;
    }
};

using RI_int_plus       = ::crucible::safety::reduce_into<int,    PlusOp>;
using RI_double_plus    = ::crucible::safety::reduce_into<double, DoublePlusOp>;

// ── Positive cases ────────────────────────────────────────────────

static_assert(is_reduce_into_v<RI_int_plus>);
static_assert(is_reduce_into_v<RI_double_plus>);

// Cv-ref stripping — every reference category resolves identically.
static_assert(is_reduce_into_v<RI_int_plus&>);
static_assert(is_reduce_into_v<RI_int_plus&&>);
static_assert(is_reduce_into_v<RI_int_plus const&>);
static_assert(is_reduce_into_v<RI_int_plus const>);
static_assert(is_reduce_into_v<RI_int_plus const&&>);

// ── Negative cases ────────────────────────────────────────────────

static_assert(!is_reduce_into_v<int>);
static_assert(!is_reduce_into_v<int*>);
static_assert(!is_reduce_into_v<int&>);
static_assert(!is_reduce_into_v<void>);
static_assert(!is_reduce_into_v<PlusOp>);

// A struct that has the same fields-by-name shape but is not
// reduce_into is rejected.
struct LookalikeReduceInto {
    int    acc;
    PlusOp op;
};
static_assert(!is_reduce_into_v<LookalikeReduceInto>);

// ── Concept form ─────────────────────────────────────────────────

static_assert(IsReduceInto<RI_int_plus>);
static_assert(IsReduceInto<RI_int_plus&&>);
static_assert(!IsReduceInto<int>);
static_assert(!IsReduceInto<PlusOp>);

// ── Accumulator + reducer extraction ─────────────────────────────

static_assert(std::is_same_v<
    reduce_into_accumulator_t<RI_int_plus>, int>);
static_assert(std::is_same_v<
    reduce_into_accumulator_t<RI_double_plus>, double>);
static_assert(std::is_same_v<
    reduce_into_reducer_t<RI_int_plus>, PlusOp>);
static_assert(std::is_same_v<
    reduce_into_reducer_t<RI_double_plus>, DoublePlusOp>);

// Cv-ref stripping — extractors both unwrap.
static_assert(std::is_same_v<
    reduce_into_accumulator_t<RI_int_plus const&>, int>);
static_assert(std::is_same_v<
    reduce_into_reducer_t<RI_int_plus&&>, PlusOp>);

// Distinct (R, Op) → distinct trait specializations; types
// agree only when they actually do.
static_assert(!std::is_same_v<
    reduce_into_accumulator_t<RI_int_plus>,
    reduce_into_accumulator_t<RI_double_plus>>);
static_assert(!std::is_same_v<
    reduce_into_reducer_t<RI_int_plus>,
    reduce_into_reducer_t<RI_double_plus>>);

}  // namespace detail::is_reduce_into_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the runtime-smoke-test discipline.  The trait is purely
// type-level; "exercising it at runtime" means computing the
// predicate value with a non-constant flag flow and confirming the
// trait's claims are not optimized into something else.

inline bool is_reduce_into_smoke_test() noexcept {
    using namespace detail::is_reduce_into_self_test;

    // Volatile-bounded loop ensures the trait reads survive
    // dead-code elimination.
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_reduce_into_v<RI_int_plus>;
        ok = ok && !is_reduce_into_v<int>;
        ok = ok && IsReduceInto<RI_int_plus&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
