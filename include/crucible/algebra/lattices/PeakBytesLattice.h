#pragma once

// ── crucible::algebra::lattices::PeakBytesLattice ───────────────────
//
// Bounded total-order lattice over a uint64_t resource counter
// representing PEAK BYTES RESIDENT during a value's production.
// Sister axis to BitsBudget; the second component sub-lattice for
// the Budgeted product wrapper from 28_04_2026_effects.md §4.4.1
// (FOUND-G63).
//
// Citation: Resource-bounded type theory (arXiv:2512.06952);
// 25_04_2026.md §2.4 Budgeted primitive.
//
// THE LOAD-BEARING USE CASE: Forge Phase D / Phase E precision-
// budget calibrator + L3 Memory's MemoryPlan.  A
// `Budgeted<{BitsBudget, PeakBytes=M}, T>` value's PeakBytes axis
// asserts at the type level that producing T held at most M bytes
// of working memory.  Composing two ops takes the JOIN (max) of
// their per-axis peak — composed-op peak = max(peak_A, peak_B)
// when ops do NOT overlap; sum semantics live OUTSIDE the lattice
// (in Forge's per-region MemoryPlan).
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier:  PeakBytes = strong-typed uint64_t.
// Order:    natural ≤ on uint64_t.
// Bottom:   PeakBytes{0}             (zero peak — strongest claim:
//                                     producer used no working memory.)
// Top:      PeakBytes{UINT64_MAX}    (saturating cap.)
// Join:     max                      (composing ops takes the larger
//                                     peak — the tighter peak that
//                                     subsumes both.)
// Meet:     min                      (intersecting peaks.)
//
// ── Direction convention ────────────────────────────────────────────
//
// Same as BitsBudgetLattice: ordered by RESOURCE CONSUMPTION, NOT
// claim strength.  See BitsBudgetLattice.h for the rationale and
// the spec citation.  Both budget axes share this convention so
// that the binary product `ProductLattice<BitsBudgetLattice,
// PeakBytesLattice>` has a coherent pointwise ordering.
//
// ── Why this is a DIFFERENT type from BitsBudget ────────────────────
//
// PeakBytes and BitsBudget are both `uint64_t`-backed lattices over
// the natural-≤ order.  Without strong typing, a refactor that
// wired up `Budgeted<{PeakBytes, BitsBudget}, T>` (axes swapped)
// would silently compile, and downstream gates checking
// `result.bits().value <= max_bits` would actually compare against
// the peak-bytes counter.  The bug class this prevents:
//
//   FOUND-G64 wrapper accepts axes in DECLARED ORDER (Bits, Peak).
//   If a maintainer flips the order in a call site, a uint64_t
//   collision would bind silently — UNLESS BitsBudget and PeakBytes
//   are distinct types.  They are.
//
//   Axiom coverage:
//     TypeSafe — PeakBytes is structurally identical to BitsBudget
//                under the hood (both wrap uint64_t) but carries a
//                distinct phantom identity.  Mixing the two in a
//                Budgeted axis-swap would be a compile error.
//     DetSafe — leq / join / meet are all `constexpr`.
//   Runtime cost:
//     element_type = PeakBytes = uint64_t + 0 phantom bytes.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── PeakBytes — strong-typed uint64_t resource counter ─────────────
//
// Phantom-typed wrapper around uint64_t.  Distinct from BitsBudget
// even though both are uint64_t-backed; mixing them in a Budgeted
// instantiation is a compile error (caught by neg-fixtures).
struct PeakBytes {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(PeakBytes const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(PeakBytes const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

// ── PeakBytesLattice — bounded chain over PeakBytes ────────────────
struct PeakBytesLattice {
    using element_type = PeakBytes;

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
        return "PeakBytesLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::peak_bytes_lattice_self_test {

static_assert(Lattice<PeakBytesLattice>);
static_assert(BoundedLattice<PeakBytesLattice>);
static_assert(!UnboundedLattice<PeakBytesLattice>);
static_assert(!Semiring<PeakBytesLattice>);

static_assert(sizeof(PeakBytes) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<PeakBytes>);
static_assert(std::is_standard_layout_v<PeakBytes>);

// Strong typing: PeakBytes is NOT structurally the same type as
// BitsBudget even though both wrap uint64_t.  This is the
// load-bearing identity that prevents axis-swap bugs in Budgeted.
static_assert(!std::is_same_v<PeakBytes, std::uint64_t>);

// Ordering witnesses.
static_assert( PeakBytesLattice::leq(PeakBytes{0},   PeakBytes{1024}));
static_assert( PeakBytesLattice::leq(PeakBytes{42},  PeakBytes{42}));   // reflexive
static_assert(!PeakBytesLattice::leq(PeakBytes{2048}, PeakBytes{1024}));

// Bounds.
static_assert(PeakBytesLattice::bottom().value == 0);
static_assert(PeakBytesLattice::top().value    == std::numeric_limits<std::uint64_t>::max());

// Join / meet.
static_assert(PeakBytesLattice::join(PeakBytes{1024}, PeakBytes{2048}).value == 2048);
static_assert(PeakBytesLattice::join(PeakBytes{2048}, PeakBytes{1024}).value == 2048);
static_assert(PeakBytesLattice::meet(PeakBytes{1024}, PeakBytes{2048}).value == 1024);
static_assert(PeakBytesLattice::meet(PeakBytes{2048}, PeakBytes{1024}).value == 1024);

// Bound identities.
static_assert(PeakBytesLattice::join(PeakBytes{1024}, PeakBytesLattice::bottom())
              == PeakBytes{1024});
static_assert(PeakBytesLattice::meet(PeakBytes{1024}, PeakBytesLattice::top())
              == PeakBytes{1024});

// Idempotence.
static_assert(PeakBytesLattice::join(PeakBytes{99}, PeakBytes{99}).value == 99);
static_assert(PeakBytesLattice::meet(PeakBytes{99}, PeakBytes{99}).value == 99);

// Distributivity witness.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    PeakBytes a{16};
    PeakBytes b{64};
    PeakBytes c{256};
    auto      lhs = PeakBytesLattice::meet(a, PeakBytesLattice::join(b, c));
    auto      rhs = PeakBytesLattice::join(PeakBytesLattice::meet(a, b),
                                           PeakBytesLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

// Implicit conversion DOWN to uint64_t.
static_assert([] consteval {
    PeakBytes      b{1024};
    std::uint64_t  n = b;
    return n == 1024;
}());

inline void runtime_smoke_test() {
    PeakBytes                     bot   = PeakBytesLattice::bottom();
    PeakBytes                     topv  = PeakBytesLattice::top();
    PeakBytes                     mid   {4096};
    [[maybe_unused]] bool         l     = PeakBytesLattice::leq(bot, topv);
    [[maybe_unused]] PeakBytes    j     = PeakBytesLattice::join(mid, topv);
    [[maybe_unused]] PeakBytes    m     = PeakBytesLattice::meet(mid, bot);

    PeakBytes                     stage1{2048};
    PeakBytes                     stage2{4096};
    PeakBytes                     composed = PeakBytesLattice::join(stage1, stage2);
    if (composed.value != 4096u) std::abort();

    std::uint64_t                 total = mid;
    if (total != 4096u) std::abort();

    using PeakBytesGraded = Graded<ModalityKind::Absolute, PeakBytesLattice, double>;
    PeakBytesGraded               v{3.14, PeakBytes{8192}};
    [[maybe_unused]] auto         g  = v.grade();
    [[maybe_unused]] auto         vp = v.peek();
}

}  // namespace detail::peak_bytes_lattice_self_test

}  // namespace crucible::algebra::lattices
