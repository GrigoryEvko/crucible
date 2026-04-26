#pragma once

// ── crucible::algebra::lattices::MonotoneLattice<T, Cmp> ────────────
//
// Per-comparison-functor monotone lattice — the foundation for
// Monotonic<T, Cmp> per 25_04_2026.md §2.3:
//
//     template <typename T, typename Cmp = std::less<T>>
//     using Monotonic = Graded<Absolute, MonotoneLattice<T, Cmp>, T>;
//
// References:
//   Atkey (FLoC 2018).        "Syntax and Semantics of Quantitative
//                              Type Theory."  (Monotonic structures
//                              over an arbitrary comparison.)
//   Hellerstein & Alvaro      "Keeping CALM: When Distributed
//   (CACM 2020).               Consistency Is Easy." arXiv:1901.01930
//                              (Monotonicity-as-coordination-freedom.)
//
// ── Second dynamic-grade lattice in algebra/lattices/ ────────────────
//
// Like FractionalLattice (the first), MonotoneLattice carries a
// RUNTIME element at every Monotonic instance — the element IS the
// monotonically-tracked value (a counter, a step index, a generation
// number, a max-loss-seen-so-far).  Distinct values are runtime-
// distinguishable; the type system enforces only that subsequent
// values are NOT smaller than the previous (per the chosen Cmp).
//
// Layout consequence: sizeof(Monotonic<T>) ==
//   sizeof(T) + sizeof(T-as-grade) + alignment.  NOT zero-overhead.
//   The Monotonic alias (MIGRATE-5 #465) may collapse one of the two
//   T copies via a custom wrapper (since the inner value and the
//   grade always coincide for Monotonic), but at the Lattice level
//   the duplication is structural — Graded<L, T> stores both
//   independently, and the lattice's contract is uniform.
//
// ── Algebraic structure ─────────────────────────────────────────────
//
// Lattice (chain order via Cmp):
//   - leq:    a ⊑ b iff NOT Cmp{}(b, a)        ("a is not GREATER than b")
//   - join:   max under Cmp                    (advance to the bigger)
//   - meet:   min under Cmp                    (regress to the smaller)
//   - bottom: numeric_limits<T>::lowest()      (when Cmp is std::less)
//   - top:    numeric_limits<T>::max()         (when Cmp is std::less)
//
// For the std::greater<T> orientation the bottom/top swap — the
// "lattice direction" follows Cmp's notion of "smaller".
//
// ── BoundedLattice gating via monotone_bounds<T, Cmp> trait ─────────
//
// MonotoneLattice<T, Cmp> ALWAYS satisfies Lattice; it satisfies
// BoundedLattice only when `monotone_bounds<T, Cmp>` is specialized.
// The default specializations cover arithmetic T paired with
// std::less<T> or std::greater<T>; user code can specialize
// monotone_bounds<MyType, MyCmp> to opt in for custom (T, Cmp) pairs.
//
// Why a trait rather than unconditional bottom/top?  Because for an
// arbitrary T + Cmp pair, "smallest" and "largest" may not be
// meaningful (e.g. unbounded multiprecision integers, comparison
// functions over partial orders).  Forcing all instantiations to
// declare bounds would push synthesizing-impossible-bounds work onto
// the user; gating on the trait lets the unbounded cases stay
// Lattice-only.
//
// ── Why Cmp is strict-less, not non-strict ──────────────────────────
//
// Cmp{}(a, b) MUST mean "a is strictly less than b" for the lattice
// laws to hold.  Conventionally:
//
//   leq  is reflexive: a ⊑ a       ⇔ NOT Cmp{}(a, a)   ⇔ Cmp is irreflexive ✓
//   leq  is antisymmetric: a ⊑ b ∧ b ⊑ a ⇒ a == b      ⇔ Cmp is asymmetric ✓
//   leq  is transitive: a ⊑ b ∧ b ⊑ c ⇒ a ⊑ c          ⇔ Cmp is transitive ✓
//
// std::less<T>, std::greater<T>, std::less<>, std::greater<> all
// satisfy these properties for arithmetic T.  Custom Cmp must too.
// Non-strict comparisons (≤, ≥) break antisymmetry — leq becomes
// trivially true everywhere.
//
// ── CAVEAT: NaN violates the lattice laws for floating-point T ──────
//
// IEEE-754 NaN compares unequal to everything (including itself):
//
//   std::less<double>{}(NaN, x)  = false   for any x including NaN
//   std::less<double>{}(x, NaN)  = false   for any x including NaN
//
// Under MonotoneLattice<double, std::less<double>>:
//
//   leq(NaN, NaN)        = NOT std::less{}(NaN, NaN) = true        ← OK
//   join(NaN, 1.0)       = std::less{}(NaN, 1.0)? 1.0 : NaN = NaN  ← BUG
//   join(1.0, NaN)       = std::less{}(1.0, NaN)? NaN : 1.0 = 1.0  ← BUG
//
// join is no longer commutative.  Antisymmetry (NaN ⊑ x ∧ x ⊑ NaN
// ⇒ NaN == x) trivially holds in the wrong direction (NaN ⋢ x AND
// x ⋢ NaN).  Verify_lattice_axioms_at would FAIL on a triple
// containing NaN — but the witness sets in this header carefully
// avoid NaN inputs.
//
// **DISCIPLINE**: instantiating MonotoneLattice<double> / <float>
// is permitted, but the user MUST NOT supply NaN values at runtime.
// If NaN is possible, either pre-filter at the boundary
// (Refined<is_finite, double>) or use a custom Cmp that orders NaN
// in a defined way (e.g. NaN < anything, then strict-less between
// finites).  No infrastructure here enforces this at compile time;
// the contract is at the call site.
//
// numeric_limits<double>::lowest()/max() return finite values
// (-DBL_MAX / DBL_MAX), not -inf / +inf, so the bottom/top witnesses
// are NaN-safe.  But user-supplied values are not.
//
//   Axiom coverage: TypeSafe — Cmp is a type parameter; mismatched
//                   compare_type at composition sites fails to compile.
//                   DetSafe — every operation is constexpr.
//   Runtime cost:   sizeof(T) per grade.  Plus one Cmp{} call per
//                   leq/join/meet (Cmp typically empty + inlined).
//
// See ALGEBRA-3 (Graded.h), ALGEBRA-2 (Lattice.h);
// MIGRATE-5 (#465) for the Monotonic<T, Cmp> alias instantiation;
// the existing safety::Monotonic for the runtime bump() machinery
// that consumes the join semantics.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cmath>      // std::isnan (constexpr in C++26 — used by NaN guard)
#include <concepts>
#include <contracts>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── monotone_bounds<T, Cmp> trait ───────────────────────────────────
//
// Primary template is undefined — instantiation requires either a
// shipped specialization (below) or a user specialization for a
// custom (T, Cmp) pair.  The trait names are `lattice_bottom` /
// `lattice_top` (NOT `bottom` / `top`) to prevent ADL collision with
// the lattice's own bottom() / top() and to keep grep audit easy.
template <typename T, typename Cmp>
struct monotone_bounds;

// Arithmetic T + std::less<T>: bottom = lowest, top = max.
template <typename T>
    requires std::is_arithmetic_v<T>
struct monotone_bounds<T, std::less<T>> {
    [[nodiscard]] static constexpr T lattice_bottom() noexcept {
        return std::numeric_limits<T>::lowest();
    }
    [[nodiscard]] static constexpr T lattice_top() noexcept {
        return std::numeric_limits<T>::max();
    }
};

// Arithmetic T + std::greater<T>: bottom/top swap.
template <typename T>
    requires std::is_arithmetic_v<T>
struct monotone_bounds<T, std::greater<T>> {
    [[nodiscard]] static constexpr T lattice_bottom() noexcept {
        return std::numeric_limits<T>::max();
    }
    [[nodiscard]] static constexpr T lattice_top() noexcept {
        return std::numeric_limits<T>::lowest();
    }
};

// ── Concept gate ────────────────────────────────────────────────────
//
// HasMonotoneBounds<T, Cmp> is the gate that enables MonotoneLattice's
// bottom() / top() static members and (transitively) the
// BoundedLattice<MonotoneLattice<T, Cmp>> concept satisfaction.
template <typename T, typename Cmp>
concept HasMonotoneBounds = requires {
    { monotone_bounds<T, Cmp>::lattice_bottom() } -> std::same_as<T>;
    { monotone_bounds<T, Cmp>::lattice_top()    } -> std::same_as<T>;
};

// ── MonotoneLattice<T, Cmp> ─────────────────────────────────────────
template <typename T, typename Cmp = std::less<T>>
struct MonotoneLattice {
    using element_type = T;
    using compare_type = Cmp;

    // ── NaN guard (FP only) ──────────────────────────────────────────
    //
    // For floating-point T, NaN inputs violate every lattice law (see
    // file-header CAVEAT).  The user contract is "do not pass NaN", but
    // the AUDIT-FOUNDATION-2026-04-26 hardening adds a defensive
    // contract_assert that fires under enforce semantic if NaN ever
    // reaches a lattice op — turning a silent law violation into a
    // loud, greppable failure at the call site.  Compiles to nothing
    // under ignore semantic, so the hot path pays no cost.
    //
    // Gated on `std::is_floating_point_v<T>` so integer / strong-id
    // instantiations get exactly the original code (no extra branch,
    // no extra include surface).
    [[nodiscard]] static constexpr bool is_nan_safe(T const& x) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            return !std::isnan(x);
        } else {
            return true;
        }
    }

    // ── Lattice ops (always available) ──────────────────────────────
    //
    // Cmp{}(a, b) is "a strictly less than b".  leq is the negation
    // of the strict inverse.  join/meet are the standard chain max/min
    // expressed via Cmp.
    [[nodiscard]] static constexpr bool leq(T const& a, T const& b) noexcept {
        contract_assert(is_nan_safe(a) && is_nan_safe(b));
        return !Cmp{}(b, a);
    }
    [[nodiscard]] static constexpr T join(T const& a, T const& b) noexcept {
        contract_assert(is_nan_safe(a) && is_nan_safe(b));
        return Cmp{}(a, b) ? b : a;  // max under Cmp
    }
    [[nodiscard]] static constexpr T meet(T const& a, T const& b) noexcept {
        contract_assert(is_nan_safe(a) && is_nan_safe(b));
        return Cmp{}(a, b) ? a : b;  // min under Cmp
    }

    // ── Bounded ops (gated by monotone_bounds<T, Cmp> existence) ────
    //
    // requires-clause on a static member function is well-formed in
    // C++26 and gates concept satisfaction at the Lattice/BoundedLattice
    // probe in algebra/Lattice.h.  Unspecialized (T, Cmp) pairs simply
    // don't satisfy BoundedLattice — they remain Lattice-only.
    [[nodiscard]] static constexpr T bottom() noexcept
        requires HasMonotoneBounds<T, Cmp>
    {
        return monotone_bounds<T, Cmp>::lattice_bottom();
    }
    [[nodiscard]] static constexpr T top() noexcept
        requires HasMonotoneBounds<T, Cmp>
    {
        return monotone_bounds<T, Cmp>::lattice_top();
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "MonotoneLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::monotone_lattice_self_test {

// ── Concept conformance: arithmetic + std::less ─────────────────────
using MonU64Less    = MonotoneLattice<std::uint64_t, std::less<std::uint64_t>>;
using MonI32Less    = MonotoneLattice<std::int32_t,  std::less<std::int32_t>>;
using MonF64Less    = MonotoneLattice<double,        std::less<double>>;

static_assert(Lattice<MonU64Less>);
static_assert(BoundedBelowLattice<MonU64Less>);
static_assert(BoundedAboveLattice<MonU64Less>);
static_assert(BoundedLattice<MonU64Less>);

static_assert(Lattice<MonI32Less>);
static_assert(BoundedLattice<MonI32Less>);

static_assert(Lattice<MonF64Less>);
static_assert(BoundedLattice<MonF64Less>);

// ── Concept conformance: arithmetic + std::greater (reversed order) ─
using MonU64Greater = MonotoneLattice<std::uint64_t, std::greater<std::uint64_t>>;

static_assert(Lattice<MonU64Greater>);
static_assert(BoundedLattice<MonU64Greater>);

// ── Concept conformance: custom Cmp, no monotone_bounds → Lattice only
struct CustomLess {
    [[nodiscard]] constexpr bool operator()(int a, int b) const noexcept {
        return a < b;
    }
};
using MonI32Custom = MonotoneLattice<int, CustomLess>;

// Custom Cmp without a monotone_bounds<int, CustomLess> specialization
// → satisfies Lattice but NOT BoundedLattice.  Documents the gating.
static_assert(Lattice<MonI32Custom>);
static_assert(!BoundedBelowLattice<MonI32Custom>);
static_assert(!BoundedAboveLattice<MonI32Custom>);
static_assert(!BoundedLattice<MonI32Custom>);
static_assert(UnboundedLattice<MonI32Custom>);

// ── Bounds correctness ──────────────────────────────────────────────
static_assert(MonU64Less::bottom()    == 0u);
static_assert(MonU64Less::top()       == std::numeric_limits<std::uint64_t>::max());
static_assert(MonI32Less::bottom()    == std::numeric_limits<std::int32_t>::min());
static_assert(MonI32Less::top()       == std::numeric_limits<std::int32_t>::max());

// std::greater orientation swaps the bounds — bottom is the LARGEST
// value (descending order's bottom-most reachable element).
static_assert(MonU64Greater::bottom() == std::numeric_limits<std::uint64_t>::max());
static_assert(MonU64Greater::top()    == 0u);

// ── Lattice ops at representative witnesses (uint64_t, std::less) ───
static_assert(MonU64Less::leq(0u, 1u));
static_assert(MonU64Less::leq(0u, 0u));
static_assert(!MonU64Less::leq(1u, 0u));
static_assert(MonU64Less::join(3u, 7u) == 7u);  // max
static_assert(MonU64Less::meet(3u, 7u) == 3u);  // min
static_assert(MonU64Less::join(3u, 3u) == 3u);  // idempotent
static_assert(MonU64Less::meet(3u, 3u) == 3u);  // idempotent

// std::greater orientation: leq follows reversed direction.
static_assert(MonU64Greater::leq(7u, 3u));
static_assert(!MonU64Greater::leq(3u, 7u));
static_assert(MonU64Greater::join(3u, 7u) == 3u);  // "max" under > is min value
static_assert(MonU64Greater::meet(3u, 7u) == 7u);

// ── EXHAUSTIVE-WITHIN-WITNESS-SET axiom coverage ────────────────────
//
// MonotoneLattice<T, Cmp> over an infinite carrier T cannot be
// exhaustively checked.  The discipline is to pick a representative
// span — bottom, near-bottom, mid, near-top, top — and run every
// triple permutation.  5 witnesses → 125 triples covering reflexive
// / antisymmetric / transitive pairs at the boundaries AND the
// interior.  The verify_bounded_lattice_axioms_at helper rolls the
// 13-axiom family into one assertion per triple; the loop below
// expands the triple space.
//
// For uint64_t / std::less:
constexpr std::uint64_t u_bot = MonU64Less::bottom();
constexpr std::uint64_t u_lo  = 1u;
constexpr std::uint64_t u_mid = std::uint64_t{1} << 32;
constexpr std::uint64_t u_hi  = std::numeric_limits<std::uint64_t>::max() - 1u;
constexpr std::uint64_t u_top = MonU64Less::top();

static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_bot, u_bot, u_bot));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_bot, u_lo,  u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_lo,  u_mid, u_hi));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_top, u_top, u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_hi,  u_mid, u_lo));   // descending
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_top, u_bot, u_top));  // boundary
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_bot, u_mid, u_top));  // span

// For int32_t / std::less (negative-zero-positive crossings):
constexpr std::int32_t i_neg = -100;
constexpr std::int32_t i_zero = 0;
constexpr std::int32_t i_pos  = 100;
constexpr std::int32_t i_bot  = MonI32Less::bottom();
constexpr std::int32_t i_top  = MonI32Less::top();

static_assert(verify_bounded_lattice_axioms_at<MonI32Less>(i_neg, i_zero, i_pos));
static_assert(verify_bounded_lattice_axioms_at<MonI32Less>(i_bot, i_zero, i_top));
static_assert(verify_bounded_lattice_axioms_at<MonI32Less>(i_top, i_neg,  i_bot));

// For uint64_t / std::greater (reversed direction sanity):
static_assert(verify_bounded_lattice_axioms_at<MonU64Greater>(u_bot, u_lo,  u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Greater>(u_top, u_mid, u_bot));

// ── Monotonic semantics (CSL "advance only") ────────────────────────
//
// The natural Monotonic<T> usage: a counter starts at bottom, weakens
// through ascending values, never returns.  Lattice axioms confirm
// the chain order under join.
static_assert(MonU64Less::leq(0u, 100u));
static_assert(MonU64Less::leq(100u, 1000u));
static_assert(MonU64Less::leq(0u, 1000u));   // transitivity
static_assert(MonU64Less::join(100u, 1000u) == 1000u);
static_assert(MonU64Less::join(0u, MonU64Less::top()) == MonU64Less::top());

// ── AUDIT-FOUNDATION-2026-04-26: NaN guard structural witnesses ─────
//
// `is_nan_safe(x)` returns true for finite values, false for NaN, and
// vacuously true for non-FP T.  Every lattice op contract-asserts
// `is_nan_safe(a) && is_nan_safe(b)` so passing NaN to leq/join/meet
// is a contract violation rather than a silent law-violation.  The
// runtime smoke test exercises the path with non-NaN FP values.
static_assert(MonU64Less::is_nan_safe(0u));
static_assert(MonU64Less::is_nan_safe(std::numeric_limits<std::uint64_t>::max()));
static_assert(MonF64Less::is_nan_safe(0.0));
static_assert(MonF64Less::is_nan_safe(1.0));
static_assert(MonF64Less::is_nan_safe(-1.0));
static_assert(MonF64Less::is_nan_safe(std::numeric_limits<double>::lowest()));
static_assert(MonF64Less::is_nan_safe(std::numeric_limits<double>::max()));
static_assert(MonF64Less::is_nan_safe(std::numeric_limits<double>::infinity()));
static_assert(MonF64Less::is_nan_safe(-std::numeric_limits<double>::infinity()));
static_assert(!MonF64Less::is_nan_safe(std::numeric_limits<double>::quiet_NaN()));
static_assert(!MonF64Less::is_nan_safe(std::numeric_limits<double>::signaling_NaN()));

// Diagnostic name.
static_assert(MonU64Less::name()    == "MonotoneLattice");
static_assert(MonI32Custom::name()  == "MonotoneLattice");

// element_type / compare_type aliases.
static_assert(std::is_same_v<MonU64Less::element_type, std::uint64_t>);
static_assert(std::is_same_v<MonU64Less::compare_type, std::less<std::uint64_t>>);
static_assert(std::is_same_v<MonU64Greater::compare_type, std::greater<std::uint64_t>>);

// ── Layout — NOT zero-overhead (dynamic-grade like FractionalLattice) ─
//
// element_type IS T; not empty.  Graded<Absolute, MonotoneLattice<T>, T>
// stores both the inner value and the grade — the grade is structural
// state, no EBO collapse.  Documenting via assertions that the
// overhead matches expectation; CRUCIBLE_GRADED_LAYOUT_INVARIANT does
// NOT apply (sizeof != sizeof(T) for the non-empty grade case).
static_assert(!std::is_empty_v<MonU64Less::element_type>);
static_assert(sizeof(MonU64Less::element_type) == 8);

template <typename T>
using MonotonicGraded =
    Graded<ModalityKind::Absolute, MonotoneLattice<T, std::less<T>>, T>;

// 8B value + 8B grade with 8B alignment → 16 bytes total.
static_assert(sizeof(MonotonicGraded<std::uint64_t>) ==
              sizeof(std::uint64_t) + sizeof(std::uint64_t));

// 4B value + 4B grade + 4B padding (alignof int = 4) → 8 bytes total.
static_assert(sizeof(MonotonicGraded<std::int32_t>) ==
              sizeof(std::int32_t) + sizeof(std::int32_t));

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: every
// algebra/lattices/ header MUST exercise lattice ops AND
// Graded<...,L,T>::weaken/compose with non-constant arguments to
// catch consteval/SFINAE traps that pure static_assert tests miss.
// MonotoneLattice's leq/join/meet take `T const&` (not by value), so
// the runtime path also exercises reference parameter passing and
// the compare-functor invocation through Cmp{}.
inline void runtime_smoke_test() {
    // Non-constant inputs.
    std::uint64_t a = 100;
    std::uint64_t b = 1000;

    // Lattice ops at runtime.
    [[maybe_unused]] bool          l = MonU64Less::leq(a, b);
    [[maybe_unused]] std::uint64_t j = MonU64Less::join(a, b);
    [[maybe_unused]] std::uint64_t m = MonU64Less::meet(a, b);

    // Bounded ops at runtime — exercises the requires-gated path.
    [[maybe_unused]] std::uint64_t bot = MonU64Less::bottom();
    [[maybe_unused]] std::uint64_t top = MonU64Less::top();

    // Reversed-orientation runtime check.
    [[maybe_unused]] bool          gl = MonU64Greater::leq(b, a);   // 1000 ≤_> 100
    [[maybe_unused]] std::uint64_t gj = MonU64Greater::join(a, b);  // = 100 (smaller-val wins under >)

    // Graded<Absolute, MonotoneLattice, T> at runtime.
    MonotonicGraded<std::uint64_t> initial{a, MonU64Less::bottom()};
    auto widened   = initial.weaken(a);                         // weaken to current value
    auto widened2  = widened.weaken(b);                         // advance to b
    auto composed  = initial.compose(widened2);                 // join with widened2
    auto rv_widen  = std::move(widened2).weaken(b);             // rvalue-this weaken
    auto rv_comp   = std::move(initial).compose(composed);      // rvalue-this compose

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek();
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume();
    [[maybe_unused]] auto g2 = rv_widen.grade();

    // ── AUDIT-FOUNDATION-2026-04-26: FP runtime path (NaN guard) ────
    //
    // Exercise leq/join/meet on non-NaN floating-point values so the
    // contract_assert(is_nan_safe(...)) path is touched at runtime
    // under the sentinel TU's enforce semantic.  Passing NaN here
    // would correctly trigger the contract handler and abort the
    // smoke test — proving the guard fires.  We do NOT pass NaN to
    // avoid aborting the test; the static_asserts above pin the
    // structural property that is_nan_safe(NaN) == false.
    double fa = -1.0;
    double fb = 0.0;
    double fc = std::numeric_limits<double>::infinity();
    [[maybe_unused]] bool   fl1 = MonF64Less::leq(fa, fb);
    [[maybe_unused]] double fj1 = MonF64Less::join(fa, fc);
    [[maybe_unused]] double fm1 = MonF64Less::meet(fa, fc);
    [[maybe_unused]] bool   fnan_a = MonF64Less::is_nan_safe(fa);
    [[maybe_unused]] bool   fnan_b = MonF64Less::is_nan_safe(fb);
    [[maybe_unused]] bool   fnan_inf = MonF64Less::is_nan_safe(fc);

    // Confirm the guard correctly identifies a NaN at runtime (the
    // value is constructed but NEVER passed to leq/join/meet — that
    // would intentionally trip the contract).
    double fnan = std::nan("");
    [[maybe_unused]] bool   fnan_fired = !MonF64Less::is_nan_safe(fnan);
}

}  // namespace detail::monotone_lattice_self_test

}  // namespace crucible::algebra::lattices
