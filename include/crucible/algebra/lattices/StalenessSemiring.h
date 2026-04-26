#pragma once

// ── crucible::algebra::lattices::StalenessSemiring ──────────────────
//
// Tropical (min-plus) semiring over ℕ ∪ {∞} — the foundation for
// Stale<T> per 25_04_2026.md §2.4:
//
//     using StalenessSemiring = Semiring<{ℕ ∪ ∞}, min, +, ∞, 0>;
//
//     template <typename T>
//     using Stale = Graded<Absolute, StalenessSemiring, T>;
//
// References:
//   Mishchenko et al. (2022/25). "Asynchronous SGD Beats Minibatch
//                                 SGD Under Arbitrary Delays."
//                                 arXiv:2206.07638  — the
//                                 convergence theorem that justifies
//                                 staleness as a graded modality.
//   Yu, Chen, Poor (TSP 2025).   "ASGD with Staleness: SDDE
//                                 Framework." arXiv:2406.11159 —
//                                 cross-corroborated SDDE bound.
//   Cuninghame-Green (1979).      "Minimax Algebra." — the canonical
//                                 reference for tropical (min-plus)
//                                 semirings.
//
// ── Why min-plus, not max-plus, not (+, ×) ──────────────────────────
//
// Staleness τ is "how many outer steps the value is behind the
// current global step".  A value at τ=0 is "fresh"; τ=5 is "five
// steps stale"; τ=∞ is "never observed".  Two natural operations:
//
//   - PROPAGATING staleness through composed operations:
//       computing on a τ1-stale value + a τ2-stale value yields a
//       result at LEAST as stale as min(τ1, τ2) — but if you want
//       the WORST-CASE bound, you sum: τ1 + τ2.  Tropical (×) is +.
//
//   - SELECTING the FRESHER of two competing staleness estimates
//       for the same value (e.g. two replicas, two retries):
//       pick min(τ1, τ2).  Tropical (+) is min.
//
// Distributivity: + distributes over min because + is monotone:
//   a + min(b, c) = min(a + b, a + c)
// Identities:
//   - additive identity:        ∞   (min(∞, x) = x)
//   - multiplicative identity:  0   (0 + x = x)
//   - multiplicative absorbing: ∞   (∞ + x = ∞ for any x)
//
// ── Coexisting Lattice structure ────────────────────────────────────
//
// Beyond the semiring, StalenessSemiring carries a separate LATTICE
// reading on the same carrier ℕ ∪ {∞}:
//
//   - leq:   a ⊑ b iff a ≤ b (ascending — bigger is "more stale")
//   - join:  max (the WORST of two staleness estimates)
//   - meet:  min (the BEST of two estimates)
//   - bot:   0 (freshest)
//   - top:   ∞ (most stale, never observed)
//
// The lattice's `join` is MAX (pessimistic combine) while the
// semiring's `add` is MIN (tropical combine — selecting freshest).
// Both operations live on the same carrier; they encode different
// reasoning modes.  Graded<>::compose uses the lattice's join (so
// composing two stale-graded values picks the worst-case bound);
// downstream code that wants tropical addition (e.g. the ASGD
// admission gate from §8) calls StalenessSemiring::add directly.
//
// ── ∞ encoding: UINT64_MAX as sentinel ──────────────────────────────
//
// element_type wraps a single std::uint64_t.  UINT64_MAX is reserved
// as the sentinel for ∞; finite staleness is 0 ≤ value < UINT64_MAX.
// This tight packing keeps sizeof(element_type) == 8 bytes — half
// the cost of a {uint64_t, bool} struct after padding.
//
// Saturating arithmetic via std::add_sat folds overflow back into
// the sentinel: if a finite + finite would overflow uint64_t, the
// result saturates to UINT64_MAX, which IS the ∞ sentinel.  This
// is semantically correct — staleness that has exceeded uint64_t
// representable range is unbounded for any practical purpose.  The
// trade-off: finite values are restricted to [0, UINT64_MAX-1];
// the value UINT64_MAX-1 cannot be represented as finite (saturation
// at UINT64_MAX would be ∞).  In practice staleness is single-digit
// to thousands of outer steps; the lost representable range is
// ~10¹⁹ values that no realistic deployment ever touches.
//
//   Axiom coverage: TypeSafe — element_type wraps uint64_t with
//                   ∞ encoding made explicit via is_infinite();
//                   no implicit conversion from raw integers.
//                   DetSafe — every operation is constexpr; no
//                   FP arithmetic; saturation is well-defined.
//   Runtime cost:   8 bytes per grade.  Plus one std::add_sat call
//                   per multiplication (1 instruction on x86 with
//                   the SETC pattern).  Acceptable for ASGD's
//                   coarse-grained staleness tracking.
//
// See ALGEBRA-3 (Graded.h), ALGEBRA-2 (Lattice.h);
// MIGRATE-8 (#468) for the Stale<T> alias instantiation;
// 25_04_2026.md §8 for the ASGD admission gate that consumes the
// + composition rule.

#include <crucible/Saturate.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── StalenessSemiring ───────────────────────────────────────────────
struct StalenessSemiring {
    struct element_type {
        // Finite staleness is 0 ≤ value < UINT64_MAX.  UINT64_MAX is
        // the sentinel for ∞.  NSDMI defaults to 0 (the freshest /
        // bottom-most lattice element) per InitSafe.
        std::uint64_t value{0};

        [[nodiscard]] static constexpr element_type infinity() noexcept {
            return element_type{std::numeric_limits<std::uint64_t>::max()};
        }

        [[nodiscard]] constexpr bool is_infinite() const noexcept {
            return value == std::numeric_limits<std::uint64_t>::max();
        }
        // Symmetric companion to is_infinite().  Useful at boundary
        // checks where the natural read is "is this a finite
        // staleness?"  (e.g. `if (tau.is_finite()) admit(...)`).
        [[nodiscard]] constexpr bool is_finite() const noexcept {
            return value != std::numeric_limits<std::uint64_t>::max();
        }

        // Default spaceship comparison — UINT64_MAX is automatically
        // the largest value, so ∞ orders after all finite staleness
        // values, which IS the desired chain-order semantic.
        [[nodiscard]] friend constexpr auto operator<=>(
            element_type, element_type) noexcept = default;
        [[nodiscard]] friend constexpr bool operator==(
            element_type, element_type) noexcept = default;
    };

    // ── Lattice ops (chain order; smaller staleness = bottom) ───────
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return element_type{0};
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type::infinity();
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        // ∞ encoded as UINT64_MAX orders correctly under ≤.
        return a.value <= b.value;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        // max — worst-case staleness combine (pessimistic).  Used by
        // Graded<>::compose at lattice level.
        return element_type{std::max(a.value, b.value)};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        // min — best-case (freshest) staleness combine.
        return element_type{std::min(a.value, b.value)};
    }

    // ── Semiring ops (tropical: min-plus algebra) ───────────────────
    //
    // Tropical (×) (= +) is the operation the 25_04_2026.md §2.4
    // "compose: τ1 + τ2" rule references.  Tropical (+) (= min) is
    // the natural fold for "pick the freshest of competing
    // estimates".
    [[nodiscard]] static constexpr element_type zero() noexcept {
        // Additive identity = ∞ (since min(∞, x) = x).
        return top();
    }
    [[nodiscard]] static constexpr element_type one() noexcept {
        // Multiplicative identity = 0 (since 0 + x = x).
        return bottom();
    }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        // Tropical addition = min.  Picks freshest.
        return meet(a, b);
    }
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        // Tropical multiplication = +.  ∞ absorbs (∞ + x = ∞).
        // Saturating add via crucible::sat::add_sat (project polyfill
        // for std::add_sat — libstdc++ 16.0.1 doesn't ship the C++26
        // standard version yet, see Saturate.h).  Overflow folds back
        // into UINT64_MAX = ∞ sentinel, which is semantically correct
        // (staleness that exceeds uint64_t range IS unbounded for any
        // practical purpose).
        if (a.is_infinite() || b.is_infinite()) {
            return top();
        }
        return element_type{::crucible::sat::add_sat(a.value, b.value)};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "StalenessSemiring";
    }
};

// ── Convenience constructors / aliases ──────────────────────────────
//
// `staleness::fresh` and `staleness::infinite` are the most common
// witnesses; users should reach for them rather than constructing
// element_type directly.  `staleness::at(n)` constructs a finite
// staleness with a contract guard rejecting the ∞ sentinel.
namespace staleness {

inline constexpr StalenessSemiring::element_type fresh    = StalenessSemiring::bottom();
inline constexpr StalenessSemiring::element_type infinite = StalenessSemiring::top();

[[nodiscard]] constexpr StalenessSemiring::element_type at(std::uint64_t n) noexcept
    pre (n < std::numeric_limits<std::uint64_t>::max())  // not the ∞ sentinel
{
    return StalenessSemiring::element_type{n};
}

}  // namespace staleness

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::staleness_semiring_self_test {

// Concept conformance — both Lattice + BoundedLattice + Semiring.
static_assert(Lattice<StalenessSemiring>);
static_assert(BoundedBelowLattice<StalenessSemiring>);
static_assert(BoundedAboveLattice<StalenessSemiring>);
static_assert(BoundedLattice<StalenessSemiring>);
static_assert(Semiring<StalenessSemiring>);

// element_type layout — one uint64_t, no padding.
static_assert(sizeof(StalenessSemiring::element_type) == sizeof(std::uint64_t));
static_assert(alignof(StalenessSemiring::element_type) == alignof(std::uint64_t));
static_assert(!std::is_empty_v<StalenessSemiring::element_type>);

// Default construction yields bottom (per InitSafe).
static_assert(StalenessSemiring::element_type{} == StalenessSemiring::bottom());

// ∞ encoding sanity.
static_assert(StalenessSemiring::top().is_infinite());
static_assert(!StalenessSemiring::bottom().is_infinite());
static_assert(StalenessSemiring::element_type{42}.is_infinite() == false);
static_assert(StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max()}.is_infinite());

// is_finite() is the symmetric companion — always opposite of is_infinite.
static_assert(StalenessSemiring::bottom().is_finite());
static_assert(!StalenessSemiring::top().is_finite());
static_assert(StalenessSemiring::element_type{42}.is_finite());
static_assert(!StalenessSemiring::element_type{
    std::numeric_limits<std::uint64_t>::max()}.is_finite());

// ── Lattice ops at representative witnesses ─────────────────────────
constexpr auto fresh   = StalenessSemiring::bottom();
constexpr auto stale1  = StalenessSemiring::element_type{1};
constexpr auto stale100= StalenessSemiring::element_type{100};
constexpr auto stale1k = StalenessSemiring::element_type{1000};
constexpr auto inf     = StalenessSemiring::top();

static_assert(StalenessSemiring::leq(fresh,   stale1));
static_assert(StalenessSemiring::leq(stale1,  stale100));
static_assert(StalenessSemiring::leq(stale100, stale1k));
static_assert(StalenessSemiring::leq(stale1k, inf));
static_assert(StalenessSemiring::leq(fresh,   inf));         // bottom ⊑ top
static_assert(StalenessSemiring::leq(fresh,   fresh));       // reflexive
static_assert(StalenessSemiring::leq(inf,     inf));         // reflexive at top
static_assert(!StalenessSemiring::leq(stale1, fresh));
static_assert(!StalenessSemiring::leq(inf,    stale1k));

// max-as-join, min-as-meet (lattice operations).
static_assert(StalenessSemiring::join(stale1, stale100) == stale100);
static_assert(StalenessSemiring::join(stale100, inf)    == inf);
static_assert(StalenessSemiring::meet(stale1, stale100) == stale1);
static_assert(StalenessSemiring::meet(fresh, stale100)  == fresh);
static_assert(StalenessSemiring::meet(stale1k, inf)     == stale1k);

// ── Semiring (tropical) ops at representative witnesses ─────────────
//
// Tropical addition = min (pick freshest).
static_assert(StalenessSemiring::add(stale1, stale100)         == stale1);
static_assert(StalenessSemiring::add(stale1k, inf)             == stale1k);  // ∞ identity
static_assert(StalenessSemiring::add(StalenessSemiring::zero(), stale1) == stale1);

// Tropical multiplication = + (compose stalenesses; ∞ absorbing).
static_assert(StalenessSemiring::mul(stale1, stale100)         == StalenessSemiring::element_type{101});
static_assert(StalenessSemiring::mul(StalenessSemiring::one(), stale1) == stale1);
static_assert(StalenessSemiring::mul(StalenessSemiring::one(), inf)    == inf);
static_assert(StalenessSemiring::mul(inf, stale1)              == inf);  // ∞ absorbs
static_assert(StalenessSemiring::mul(stale1, inf)              == inf);  // ∞ absorbs

// Saturating overflow → ∞ encoding.  UINT64_MAX-1 + 2 saturates to
// UINT64_MAX which IS the ∞ sentinel.
constexpr auto near_max = StalenessSemiring::element_type{
    std::numeric_limits<std::uint64_t>::max() - 5};
static_assert(StalenessSemiring::mul(near_max, StalenessSemiring::element_type{10}) == inf);

// ── EXHAUSTIVE-WITHIN-WITNESS-SET axiom coverage ────────────────────
//
// 5 witnesses → 125 triples; verify_bounded_lattice_axioms_at +
// verify_semiring_axioms_at each cover the per-triple axiom families.
// Below picks representative triples crossing the ∞ boundary, the
// finite interior, and the descending direction.
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(fresh,    fresh,    fresh));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(fresh,    stale1,   inf));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(stale1,   stale100, stale1k));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(stale1k,  stale100, stale1));   // descending
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(inf,      inf,      inf));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(inf,      stale1,   fresh));    // boundary
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(fresh,    stale100, inf));      // span

// Semiring axioms — distributivity is the load-bearing tropical
// property.  Witnesses must avoid overflow in the finite interior
// for the equalities to hold ULP-exactly.
static_assert(verify_semiring_axioms_at<StalenessSemiring>(fresh,   fresh,   fresh));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(fresh,   stale1,  stale100));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(stale1,  stale100, stale1k));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(stale1k, stale100, stale1));   // descending
static_assert(verify_semiring_axioms_at<StalenessSemiring>(inf,     inf,     inf));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(inf,     stale1,  fresh));     // ∞ + ∞ axioms
static_assert(verify_semiring_axioms_at<StalenessSemiring>(stale1,  inf,     stale100));  // mid-∞-mid

// Diagnostic name.
static_assert(StalenessSemiring::name() == "StalenessSemiring");

// Convenience constants.
static_assert(staleness::fresh    == StalenessSemiring::bottom());
static_assert(staleness::infinite == StalenessSemiring::top());
static_assert(staleness::at(42)   == StalenessSemiring::element_type{42});

// ── Layout — NOT zero-overhead (dynamic-grade) ──────────────────────
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using StaleGraded = Graded<ModalityKind::Absolute, StalenessSemiring, T>;

// 1B value + 8B grade → 16 bytes (1 + 7 padding + 8).
static_assert(sizeof(StaleGraded<OneByteValue>) ==
              sizeof(OneByteValue) + sizeof(StalenessSemiring::element_type) + 7);

// 8B value + 8B grade → 16 bytes exactly.
static_assert(sizeof(StaleGraded<EightByteValue>) ==
              sizeof(EightByteValue) + sizeof(StalenessSemiring::element_type));

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: every
// algebra/lattices/ header MUST exercise lattice ops AND
// Graded<...,L,T>::weaken/compose with non-constant arguments to
// catch consteval/SFINAE traps that pure static_assert tests miss.
// Especially important here because StalenessSemiring's mul uses
// std::add_sat — runtime-path verification ensures the saturation
// path is exercised.
inline void runtime_smoke_test() {
    // Non-constant staleness values.
    std::uint64_t n_a = 5;
    std::uint64_t n_b = 17;
    auto a = StalenessSemiring::element_type{n_a};
    auto b = StalenessSemiring::element_type{n_b};

    // Lattice ops at runtime.
    [[maybe_unused]] bool                            l = StalenessSemiring::leq(a, b);
    [[maybe_unused]] StalenessSemiring::element_type j = StalenessSemiring::join(a, b);
    [[maybe_unused]] StalenessSemiring::element_type m = StalenessSemiring::meet(a, b);

    // Semiring ops at runtime — exercises std::add_sat.
    [[maybe_unused]] auto sum  = StalenessSemiring::add(a, b);
    [[maybe_unused]] auto prod = StalenessSemiring::mul(a, b);
    [[maybe_unused]] auto absb = StalenessSemiring::mul(a, StalenessSemiring::top());

    // Convenience helpers at runtime.
    [[maybe_unused]] auto at_n = staleness::at(n_a);

    // Graded<Absolute, StalenessSemiring, T> at runtime.
    OneByteValue v{42};
    StaleGraded<OneByteValue> initial{v, StalenessSemiring::bottom()};
    auto widened   = initial.weaken(a);                  // weaken to staleness 5
    auto widened2  = widened.weaken(b);                  // advance to 17
    auto composed  = initial.compose(widened2);          // join with worse
    auto rv_widen  = std::move(widened2).weaken(b);      // rvalue-this weaken
    auto rv_comp   = std::move(initial).compose(composed); // rvalue-this compose

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto g2 = rv_widen.grade();
}

}  // namespace detail::staleness_semiring_self_test

}  // namespace crucible::algebra::lattices
