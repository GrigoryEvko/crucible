// FIXY-V-252 sentinel TU: algebra/lattices/BarrierStrengthLattice.h —
// total-order CHAIN over memory-fence strength tiers (None ⊑
// CompilerBarrier ⊑ AcquireLoad ⊑ ReleaseStore ⊑ AcqRel ⊑ SeqCst ⊑
// FullFence).
//
// V-252 ships ONLY the lattice (the algebraic substrate): the enum +
// ChainLatticeOps + At<K> singletons + reflection cardinality/name
// coverage + exhaustive chain-axiom + distributivity verification.  It
// ships NO value-level machinery and NO DimensionAxis enumerator:
//   - FIXY-V-253 ships DimensionAxis::BarrierStrength (the axis
//     enumerator) — so this TU does NOT reference safety/DimensionTraits.h.
//   - FIXY-V-255 ships safety/BarrierGuarded.h (the Graded<Absolute,
//     BarrierStrengthLattice::At<K>, P> wrapper composed with the Arch
//     axis) PLUS the row_hash_contribution<safety::BarrierGuarded<...>>
//     federation-cache discriminator in safety/diag/RowHashFold.h —
//     EVERY row_hash specialization is wrapper-keyed and lives there;
//     the lattice layer pulls NO row_hash, and `safety::BarrierGuarded`
//     does not exist until V-255, so the row_hash is deferred by
//     construction (mirrors VendorLattice → safety/Vendor.h and
//     HwInstructionLattice (V-251) → safety/Hw.h (V-254)).
//   - FIXY-V-263 ships the ChaseLevDeque barrier annotation that consumes
//     the explicit barrier<Portable, Release> grant keyed off this axis.
//
// Why a SEPARATE axis from MemOrderLattice (FOUND-G28): MemOrder models
// the C++ std::memory_order TAG on an atomic op (inverted convention,
// SeqCst at bottom).  BarrierStrength is the HW-fence capability ladder
// (standard convention, None at bottom) bracketed below by None /
// CompilerBarrier (not memory_order values) and above by FullFence
// (a standalone mfence/DMB-ISH).  Distinct axes, distinct gates.
//
// On AcquireLoad ⊑ ReleaseStore: C++ acquire/release are formally
// incomparable; V-252 deliberately linearizes them into a strength
// ladder for admission gating (the formal correctness is carried by the
// explicit barrier<> grant, not derived from the lattice).  See the
// header doc-block for the full rationale.
//
// Negative coverage lives in test/safety_neg/neg_barrier_strength_*.cpp.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/BarrierStrengthLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;

namespace {

using cal::BarrierStrength;
using L = cal::BarrierStrengthLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>);
static_assert(crucible::algebra::BoundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-252: a chain order carries no independent ⊕/⊗ — the 'Tier-S "
    "Semiring' classification is the AXIS tier (V-253), not a Semiring "
    "concept on the lattice itself.");

// ── Cardinality — 7 tiers ───────────────────────────────────────────
static_assert(cal::detail::barrier_strength_lattice_self_test::barrier_strength_count == 7,
    "FIXY-V-252: BarrierStrength must have exactly 7 enumerators.");

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>);

// ── Ordinal progression (weakest → strongest) ──────────────────────
static_assert(std::to_underlying(BarrierStrength::None)            == 0, "bottom — no barrier");
static_assert(std::to_underlying(BarrierStrength::CompilerBarrier) == 1);
static_assert(std::to_underlying(BarrierStrength::AcquireLoad)     == 2);
static_assert(std::to_underlying(BarrierStrength::ReleaseStore)    == 3);
static_assert(std::to_underlying(BarrierStrength::AcqRel)          == 4);
static_assert(std::to_underlying(BarrierStrength::SeqCst)          == 5);
static_assert(std::to_underlying(BarrierStrength::FullFence)       == 6, "top — standalone fence");

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == BarrierStrength::None);
static_assert(L::top()    == BarrierStrength::FullFence);

// ── Chain subsumption (every tier admits everything below) ──────────
static_assert(L::leq(BarrierStrength::None,            BarrierStrength::CompilerBarrier));
static_assert(L::leq(BarrierStrength::CompilerBarrier, BarrierStrength::AcquireLoad));
static_assert(L::leq(BarrierStrength::AcquireLoad,     BarrierStrength::ReleaseStore));
static_assert(L::leq(BarrierStrength::ReleaseStore,    BarrierStrength::AcqRel));
static_assert(L::leq(BarrierStrength::AcqRel,          BarrierStrength::SeqCst));
static_assert(L::leq(BarrierStrength::SeqCst,          BarrierStrength::FullFence));
static_assert(L::leq(BarrierStrength::None,            BarrierStrength::FullFence), "transitive endpoints");

// The load-bearing satisfies witnesses from the task spec:
static_assert(L::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst),
    "FIXY-V-252: SeqCst::satisfies<AcqRel> = true (stronger satisfies weaker).");
static_assert(!L::leq(BarrierStrength::SeqCst, BarrierStrength::AcqRel),
    "FIXY-V-252: AcqRel does NOT satisfy a SeqCst requirement — reverse false.");
static_assert(L::leq(BarrierStrength::CompilerBarrier, BarrierStrength::FullFence),
    "FIXY-V-252: a FullFence satisfies a CompilerBarrier requirement.");
static_assert(!L::leq(BarrierStrength::FullFence, BarrierStrength::None),
    "FIXY-V-252: a FullFence claim is NEVER reduced to a no-barrier requirement.");

// ── Join — par=join (stronger-fence-dominates) ──────────────────────
static_assert(L::join(BarrierStrength::CompilerBarrier, BarrierStrength::SeqCst)
              == BarrierStrength::SeqCst,
    "FIXY-V-252: composing a compiler-barrier site with a SeqCst site "
    "yields SeqCst — the region's fence strength is the LUB.");
static_assert(L::join(BarrierStrength::None, BarrierStrength::AcquireLoad)
              == BarrierStrength::AcquireLoad, "None is the join identity");
static_assert(L::join(BarrierStrength::FullFence, BarrierStrength::AcqRel)
              == BarrierStrength::FullFence, "FullFence absorbs in join");

// ── Meet — and=meet (weaker-fence-floor) ───────────────────────────
static_assert(L::meet(BarrierStrength::FullFence, BarrierStrength::AcquireLoad)
              == BarrierStrength::AcquireLoad,
    "FIXY-V-252: meeting a strong binding with a weak policy yields the floor.");
static_assert(L::meet(BarrierStrength::None, BarrierStrength::SeqCst)
              == BarrierStrength::None, "None absorbs in meet");

// ── At<K> singletons — empty element_type, EBO collapse, tier pin ───
static_assert(crucible::algebra::Lattice<L::At<BarrierStrength::AcqRel>>);
static_assert(crucible::algebra::BoundedLattice<L::At<BarrierStrength::FullFence>>);
static_assert(std::is_empty_v<L::At<BarrierStrength::None>::element_type>,
    "FIXY-V-252: At<None>::element_type must be empty so Graded<Absolute, "
    "At<None>, P> EBO-collapses to sizeof(P).");
static_assert(std::is_empty_v<L::At<BarrierStrength::SeqCst>::element_type>);
static_assert(std::is_empty_v<L::At<BarrierStrength::FullFence>::element_type>);
static_assert(L::At<BarrierStrength::SeqCst>::tier == BarrierStrength::SeqCst,
    "FIXY-V-252: At<K>::tier must equal K at the type level so the V-255 "
    "wrapper reads the pinned tier with no runtime data.");

// ── EBO collapse witness — Graded<Absolute, At<K>, P> == sizeof(P) ──
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     L::At<BarrierStrength::SeqCst>, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-252: regime-1 EBO collapse — pinning a SeqCst tier adds zero "
    "bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     L::At<BarrierStrength::FullFence>, int>)
    == sizeof(int));

// ── Names ───────────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"BarrierStrengthLattice"});
static_assert(L::At<BarrierStrength::AcqRel>::name()
              == std::string_view{"BarrierStrengthLattice::At<AcqRel>"});
static_assert(L::At<BarrierStrength::CompilerBarrier>::name()
              == std::string_view{"BarrierStrengthLattice::At<CompilerBarrier>"});
static_assert(cal::barrier_strength_name(BarrierStrength::FullFence)
              == std::string_view{"FullFence"});

}  // namespace

int main() {
    cal::detail::barrier_strength_lattice_self_test
        ::barrier_strength_lattice_runtime_smoke_test();
    return 0;
}
