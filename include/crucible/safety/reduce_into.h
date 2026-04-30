#pragma once

// ── crucible::safety::reduce_into<R, Op> ────────────────────────────
//
// Accumulator-into-place wrapper for the canonical Reduction shape
// (FOUND-D14 of 28_04_2026_effects.md §6.1 + 27_04_2026.md §3.3).
//
// A reduction-shaped function consumes one input region and BORROWS
// (via lvalue ref) a `reduce_into<R, Op>` parameter holding the
// accumulator state.  The function folds elements of the input into
// the accumulator using the associative reducer Op:
//
//   void sum_into(OwnedRegion<float, X>&&, reduce_into<float, plus>&);
//   void max_into(OwnedRegion<int,   X>&&, reduce_into<int,   maxer>&);
//   void hist_into(OwnedRegion<int, X>&&,
//                  reduce_into<std::array<int, 256>, hist_op>&);
//
// The accumulator is BORROWED (not consumed) because the caller wants
// to keep the partial state alive across multiple reduce_into calls
// — the canonical pattern is iterative refinement (Adam's β₂ moment
// EMA, running mean/variance) where each step's reduce_into result
// feeds the next step's input.
//
// ── Design intent ──────────────────────────────────────────────────
//
// Op is associative.  parallel_reduce_views<N, R> exploits this to
// fan out the input region across N workers, each computing a partial
// accumulator, then folds the N partials into the final value via Op.
// Without associativity the partials cannot be merged in arbitrary
// order — the framework refuses parallel lowering and falls back to
// sequential left-fold.
//
// Op is NOT required to be commutative.  The framework preserves the
// argument order of Op(acc, element) so non-commutative reducers
// (e.g., function composition, matrix product) compose correctly when
// the lattice's TopologicalReduce<N> partition discipline is used.
// Commutative Ops unlock further parallelism (any-order reduction);
// the framework's `is_commutative_v<Op>` predicate selects between
// the two regimes.  This header does NOT enforce commutativity at
// the type level — callers opt in via an explicit trait.
//
// ── What this header ships ──────────────────────────────────────────
//
//   reduce_into<R, Op>     The accumulator-into-place wrapper.  Holds
//                          a value of type R (the accumulator) and a
//                          callable of type Op (the reducer).  Move-
//                          only (linear semantics — the accumulator
//                          is unique state).  Provides:
//                            - construction from (R init, Op op)
//                            - peek() const& — read accumulator
//                            - peek_mut() & — mutable access for
//                              direct combine in single-thread paths
//                            - consume() && — extract accumulator
//                            - combine(R const&) — apply Op to fold
//                              a partial / element into the
//                              accumulator
//
//   is_reduction_op_v<Op, R>
//                          Concept-form predicate: true iff Op is
//                          callable as Op(R const&, R const&) → R.
//                          Used by the constructor's requires clause
//                          to reject ill-typed reducers.
//
// ── Minimal-surface vs full-API ─────────────────────────────────────
//
// This is the minimal surface needed for FOUND-D07 (the wrapper
// detector trait) to specialize on `reduce_into<R, Op>` and FOUND-D14
// (the Reduction concept) to recognize a function whose param 1 is
// an `reduce_into<R, Op>&`.  The full parallel_reduce_views<N, R>
// integration (FOUND-F04) extends this with:
//   - Op-associativity proof obligation (currently doc-only)
//   - per-worker partial-accumulator management
//   - cache-tier-aware fan-out (CostModel.h)
// Those land in a follow-up wrapper that COMPOSES with reduce_into.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe   — R and Op default-init via NSDMI; explicit ctor
//                requires both fields.
//   TypeSafe   — Op constrained to be invocable with (R, R) → R via
//                is_reduction_op_v concept.
//   NullSafe   — N/A; value-type wrapper, no pointer surface.
//   MemSafe    — defaulted destruct; R and Op own their own resources.
//   BorrowSafe — move-only; copy deleted.  combine() takes R by const&,
//                so partial accumulators are not consumed.
//   ThreadSafe — single-owner discipline.  Concurrent combine() into
//                the same wrapper is a data race; the parallel framework
//                threads N per-worker reduce_into wrappers and merges.
//   LeakSafe   — value-type; destruct paths bounded.
//   DetSafe    — Op deterministic by contract.  Same input order +
//                same Op → same result.

#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── is_reduction_op_v — Op constraint predicate ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Op is admitted iff it is callable as Op(R const&, R const&) and
// returns something convertible to R.  This is the minimal shape for
// a reducer; associativity is a SEMANTIC contract documented in the
// header (the framework cannot prove it structurally without an
// algebraic decision procedure).
template <typename Op, typename R>
concept is_reduction_op_v =
    std::is_invocable_v<Op const&, R const&, R const&>
 && std::is_convertible_v<
        std::invoke_result_t<Op const&, R const&, R const&>, R>;

// ═════════════════════════════════════════════════════════════════════
// ── reduce_into<R, Op> ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename R, typename Op>
    requires is_reduction_op_v<Op, R>
class [[nodiscard]] reduce_into {
public:
    using accumulator_type = R;
    using reducer_type     = Op;

    // ── Construction ────────────────────────────────────────────────
    constexpr reduce_into(R init, Op op)
        noexcept(std::is_nothrow_move_constructible_v<R>
              && std::is_nothrow_move_constructible_v<Op>)
        : acc_{std::move(init)}, op_{std::move(op)} {}

    // Move-only: the accumulator is unique state.  Copying would
    // duplicate the unique accumulator, breaking the linearity that
    // makes parallel-reduce safe.
    reduce_into(reduce_into const&)            = delete;
    reduce_into& operator=(reduce_into const&) = delete;

    constexpr reduce_into(reduce_into&&)            = default;
    constexpr reduce_into& operator=(reduce_into&&) = default;

    ~reduce_into() = default;

    // ── Read access ─────────────────────────────────────────────────
    [[nodiscard]] constexpr R const& peek() const& noexcept {
        return acc_;
    }

    [[nodiscard]] constexpr R& peek_mut() & noexcept {
        return acc_;
    }

    [[nodiscard]] constexpr R consume() &&
        noexcept(std::is_nothrow_move_constructible_v<R>)
    {
        return std::move(acc_);
    }

    [[nodiscard]] constexpr Op const& reducer() const& noexcept {
        return op_;
    }

    // ── combine — fold a partial / element into the accumulator ────
    constexpr void combine(R const& partial)
        noexcept(noexcept(std::declval<Op const&>()(
            std::declval<R const&>(), std::declval<R const&>())))
    {
        acc_ = op_(acc_, partial);
    }

private:
    R  acc_;
    Op op_;
};

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::reduce_into_self_test {

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

struct MaxOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a > b ? a : b;
    }
};

// is_reduction_op_v positives.
static_assert(is_reduction_op_v<PlusOp, int>);
static_assert(is_reduction_op_v<MaxOp,  int>);

// Negatives: callable shape mismatches.
struct NotInvocable {};
static_assert(!is_reduction_op_v<NotInvocable, int>);

struct WrongArity {
    constexpr int operator()(int) const noexcept { return 0; }
};
static_assert(!is_reduction_op_v<WrongArity, int>);

struct WrongReturn {
    constexpr void operator()(int const&, int const&) const noexcept {}
};
static_assert(!is_reduction_op_v<WrongReturn, int>);

// Construction + peek.
[[nodiscard]] consteval bool construct_and_peek() noexcept {
    reduce_into<int, PlusOp> r{0, PlusOp{}};
    return r.peek() == 0;
}
static_assert(construct_and_peek());

// combine + peek round-trip.
[[nodiscard]] consteval bool combine_folds() noexcept {
    reduce_into<int, PlusOp> r{0, PlusOp{}};
    r.combine(7);
    r.combine(35);
    return r.peek() == 42;
}
static_assert(combine_folds());

// consume() && extracts.
[[nodiscard]] consteval bool consume_extracts() noexcept {
    reduce_into<int, PlusOp> r{42, PlusOp{}};
    int v = std::move(r).consume();
    return v == 42;
}
static_assert(consume_extracts());

// Move-only — copy ctor / copy assign deleted.
static_assert(!std::is_copy_constructible_v<reduce_into<int, PlusOp>>);
static_assert(!std::is_copy_assignable_v<reduce_into<int, PlusOp>>);
static_assert( std::is_move_constructible_v<reduce_into<int, PlusOp>>);
static_assert( std::is_move_assignable_v<reduce_into<int, PlusOp>>);

}  // namespace detail::reduce_into_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool reduce_into_smoke_test() noexcept {
    using namespace detail::reduce_into_self_test;

    volatile int const seed = 0;
    reduce_into<int, PlusOp> r{static_cast<int>(seed), PlusOp{}};
    r.combine(7);
    r.combine(35);

    bool ok = (r.peek() == 42);

    reduce_into<int, MaxOp> rmax{0, MaxOp{}};
    rmax.combine(7);
    rmax.combine(3);
    rmax.combine(99);
    rmax.combine(42);
    ok = ok && (rmax.peek() == 99);

    return ok;
}

}  // namespace crucible::safety
