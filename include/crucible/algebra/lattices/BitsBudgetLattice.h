#pragma once

// ── crucible::algebra::lattices::BitsBudgetLattice ──────────────────
//
// Bounded total-order lattice over a uint64_t resource counter
// representing BITS TRANSFERRED across a value's production / use
// path.  One of two component sub-lattices for the Budgeted product
// wrapper from 28_04_2026_effects.md §4.4.1 (FOUND-G63).
//
// Citation: Resource-bounded type theory (arXiv:2512.06952);
// 25_04_2026.md §2.4 Budgeted primitive.
//
// THE LOAD-BEARING USE CASE: Forge Phase D / Phase E precision-
// budget calibrator.  A `Budgeted<{BitsBudget=N, PeakBytes=M}, T>`
// value asserts at the type level that producing T transferred at
// most N bits and held at most M bytes resident.  Composing two
// Budgeted operations (chaining ops) computes the JOIN (max) of
// their per-axis budgets — the resulting value's budget is bounded
// above by the larger of the two inputs.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier:  BitsBudget = strong-typed uint64_t.
// Order:    natural ≤ on uint64_t.
// Bottom:   BitsBudget{0}             (zero bits transferred — the
//                                      strongest claim about resource
//                                      consumption: the producer
//                                      consumed nothing.)
// Top:      BitsBudget{UINT64_MAX}    (saturating cap — admits any
//                                      reasonable consumption.)
// Join:     max                       (composing two ops takes the
//                                      LARGER of their budgets.)
// Meet:     min                       (intersecting tightens to the
//                                      smaller bound.)
//
// ── Direction convention ────────────────────────────────────────────
//
// This lattice is ordered by RESOURCE CONSUMPTION, NOT by claim
// strength.  Larger numeric value ⟹ higher in the lattice.  This
// is OPPOSITE to the chain wrappers (DetSafe/HotPath/Crash) where
// "stronger claim = top".  The justification:
//
//   - The grade carries an ACTUAL transferred-bytes count, not a
//     claim CAP.  Composing two transfers ADDS bytes (via Forge's
//     budget-tracking logic outside this lattice); the lattice
//     answers "is T1's footprint ≤ T2's footprint?" — a structural
//     question about resource ordering, not about admission gates.
//
//   - Budget-comparison gates downstream read the wrapped value as
//     "produced ≤ N bits"; admission requires source.budget ≤ gate.
//     With leq = ≤, the natural reading is "source ⊑ gate iff
//     source's footprint is below the gate's tolerance".
//
//   - Resource-bounded type theory (arXiv:2512.06952) frames it the
//     same way: the modal grade is a USAGE count and the lattice
//     order matches numeric ≤.
//
// A future maintainer wanting "claim-strength" semantics (smaller =
// stronger, project chain-wrapper convention) should NOT mutate
// this lattice — they should ship a SEPARATE BitsCap lattice with
// inverted ordering.  Mixing the two interpretations in one lattice
// breaks every downstream Budgeted call site.
//
//   Axiom coverage:
//     TypeSafe — BitsBudget is a strong-tagged uint64_t; mixing
//                BitsBudget with PeakBytes is a compile error
//                (different newtype, distinct phantom tag).
//     DetSafe — leq / join / meet are all `constexpr`; deterministic
//                in their arguments.
//   Runtime cost:
//     element_type = BitsBudget = uint64_t + 0 phantom bytes.  When
//     composed in `Graded<Absolute, ProductLattice<BitsBudgetLattice,
//     PeakBytesLattice>, T>`, the budget pair is regime-4: 16 bytes
//     of grade carried per instance (non-empty, distinct from the
//     chain wrappers' regime-1 EBO-collapsed cases).
//
// See FOUND-G63 (this file) for the lattice; FOUND-G64
// (safety/Budgeted.h) for the wrapper; PeakBytesLattice.h for the
// sister axis; ProductLattice.h for the binary composition.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── BitsBudget — strong-typed uint64_t resource counter ────────────
//
// The phantom-typed wrapper around uint64_t that gives the lattice's
// element_type its identity.  Mixing with PeakBytes (the sister
// axis) is a compile error — both are uint64_t under the hood, but
// distinct C++ types prevent silent assignment.
//
// Implicit conversion to uint64_t IS provided (one-way) so that
// existing accumulator code (`total += bits.value`) continues to
// compile; the reverse direction (uint64_t → BitsBudget) requires
// `BitsBudget{n}` explicit construction.
struct BitsBudget {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(BitsBudget const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(BitsBudget const&) const noexcept = default;

    // One-way conversion to the underlying uint64 for arithmetic
    // helpers (running totals, fmix64 hashes, etc.).  The reverse
    // direction is intentionally explicit-only.
    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

// ── BitsBudgetLattice — bounded chain over BitsBudget ──────────────
struct BitsBudgetLattice {
    using element_type = BitsBudget;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return element_type{0};
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return a.value <= b.value;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value >= b.value ? a.value : b.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{a.value <= b.value ? a.value : b.value};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "BitsBudgetLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::bits_budget_lattice_self_test {

static_assert(Lattice<BitsBudgetLattice>);
static_assert(BoundedLattice<BitsBudgetLattice>);
static_assert(!UnboundedLattice<BitsBudgetLattice>);
static_assert(!Semiring<BitsBudgetLattice>);

static_assert(sizeof(BitsBudget) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<BitsBudget>);
static_assert(std::is_standard_layout_v<BitsBudget>);

// Ordering witnesses.
static_assert( BitsBudgetLattice::leq(BitsBudget{0},   BitsBudget{1}));
static_assert( BitsBudgetLattice::leq(BitsBudget{1},   BitsBudget{1}));   // reflexive
static_assert(!BitsBudgetLattice::leq(BitsBudget{2},   BitsBudget{1}));
static_assert( BitsBudgetLattice::leq(BitsBudgetLattice::bottom(),
                                       BitsBudgetLattice::top()));

// Bounds.
static_assert(BitsBudgetLattice::bottom().value == 0);
static_assert(BitsBudgetLattice::top().value    == std::numeric_limits<std::uint64_t>::max());

// Join / meet.
static_assert(BitsBudgetLattice::join(BitsBudget{3}, BitsBudget{7}).value == 7);
static_assert(BitsBudgetLattice::join(BitsBudget{7}, BitsBudget{3}).value == 7);   // commutative
static_assert(BitsBudgetLattice::meet(BitsBudget{3}, BitsBudget{7}).value == 3);
static_assert(BitsBudgetLattice::meet(BitsBudget{7}, BitsBudget{3}).value == 3);

// Bound identities.
static_assert(BitsBudgetLattice::join(BitsBudget{42}, BitsBudgetLattice::bottom())
              == BitsBudget{42});
static_assert(BitsBudgetLattice::meet(BitsBudget{42}, BitsBudgetLattice::top())
              == BitsBudget{42});
static_assert(BitsBudgetLattice::join(BitsBudgetLattice::top(), BitsBudget{42})
              == BitsBudgetLattice::top());
static_assert(BitsBudgetLattice::meet(BitsBudgetLattice::bottom(), BitsBudget{42})
              == BitsBudgetLattice::bottom());

// Idempotence.
static_assert(BitsBudgetLattice::join(BitsBudget{99}, BitsBudget{99}).value == 99);
static_assert(BitsBudgetLattice::meet(BitsBudget{99}, BitsBudget{99}).value == 99);

// Antisymmetry witnesses.
static_assert( BitsBudgetLattice::leq(BitsBudget{5}, BitsBudget{5}));
static_assert(!(BitsBudget{5} != BitsBudget{5}));

// Distributivity at three witnesses (full chain order is distributive,
// but spot-check at non-bottom / non-top values).
[[nodiscard]] consteval bool distributive_witness() noexcept {
    BitsBudget a{2};
    BitsBudget b{5};
    BitsBudget c{8};
    auto       lhs = BitsBudgetLattice::meet(a, BitsBudgetLattice::join(b, c));
    auto       rhs = BitsBudgetLattice::join(BitsBudgetLattice::meet(a, b),
                                             BitsBudgetLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

// Strong-typing rejection — BitsBudget != raw uint64_t at the
// element_type level (the leq signature would fail to bind a
// uint64_t directly without the explicit BitsBudget{} construction).
//
// Verified at the wrapper / neg-compile level; here we just check
// that the struct identity is not std::uint64_t.
static_assert(!std::is_same_v<BitsBudget, std::uint64_t>);

// Implicit conversion DOWN to uint64_t works for arithmetic helpers.
static_assert([] consteval {
    BitsBudget        b{42};
    std::uint64_t     n = b;     // implicit one-way conversion
    return n == 42;
}());

// Reverse direction (uint64_t → BitsBudget) requires explicit
// construction.  Verified by neg-compile fixtures; no compile-time
// witness is possible without instantiating the rejection.

inline void runtime_smoke_test() {
    BitsBudget                    bot   = BitsBudgetLattice::bottom();
    BitsBudget                    topv  = BitsBudgetLattice::top();
    BitsBudget                    mid   {1024};
    [[maybe_unused]] bool         l     = BitsBudgetLattice::leq(bot, topv);
    [[maybe_unused]] BitsBudget   j     = BitsBudgetLattice::join(mid, topv);
    [[maybe_unused]] BitsBudget   m     = BitsBudgetLattice::meet(mid, bot);

    // Chain progression: each step's budget grows.
    BitsBudget                    step1{4};
    BitsBudget                    step2{8};
    BitsBudget                    composed = BitsBudgetLattice::join(step1, step2);
    if (composed.value != 8u) std::abort();

    // Implicit unwrap for accumulator-style math.
    std::uint64_t                 total = mid;
    if (total != 1024u) std::abort();

    // Lattice over Graded substrate.
    using BitsBudgetGraded = Graded<ModalityKind::Absolute, BitsBudgetLattice, int>;
    BitsBudgetGraded              v{42, BitsBudget{16}};
    [[maybe_unused]] auto         g  = v.grade();
    [[maybe_unused]] auto         vp = v.peek();
}

}  // namespace detail::bits_budget_lattice_self_test

}  // namespace crucible::algebra::lattices
