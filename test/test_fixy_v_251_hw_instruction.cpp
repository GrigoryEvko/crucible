// FIXY-V-251 sentinel TU: algebra/lattices/HwInstructionLattice.h —
// total-order CHAIN over hardware-instruction capability tiers
// (NoneAllowed ⊑ Scalar ⊑ Vectorizable ⊑ NonDeterministicTsc ⊑
// PrivilegedMsr).
//
// V-251 ships ONLY the lattice (the algebraic substrate): the enum +
// ChainLatticeOps + At<T> singletons + reflection-driven cardinality/
// name-coverage + exhaustive chain-axiom + distributivity verification.
// It ships NO value-level machinery and NO DimensionAxis enumerator:
//   - FIXY-V-253 ships DimensionAxis::HwInstruction (the axis
//     enumerator) — so this TU does NOT reference safety/DimensionTraits.h
//     (the axis does not exist yet).
//   - FIXY-V-254 ships safety/Hw.h (the Graded<Absolute,
//     HwInstructionLattice::At<Tier>, P> regime-1 carrier) PLUS the
//     row_hash_contribution<safety::Hw<Tier, Inner>> federation-cache
//     discriminator in safety/diag/RowHashFold.h — exactly how
//     VendorLattice defers to safety/Vendor.h and SimdIsaLattice (V-250)
//     defers to safety/SimdWidthPinned.h (V-256).  EVERY row_hash
//     specialization in the codebase is wrapper-keyed and lives in
//     RowHashFold.h; the lattice layer pulls NO row_hash machinery, and
//     `safety::Hw` does not exist until V-254 — so the row_hash deliverable
//     is deferred by construction, not omitted.
//   - FIXY-V-260 ships the H001/H002/H003 CollisionCatalog rules
//     (e.g. PrivilegedMsr × ¬Permission<warden::tag::Root> reject).
//
// Why a dedicated HwInstruction axis (Tier 0 — BLOCKER for Mimic): the
// Met(X) effect row records MEMORY effects; it cannot say WHICH hardware
// instruction class a kernel issues.  Mimic must know per kernel whether
// scalar / SIMD / rdtsc / ring-0 MSR instructions are emitted before it
// can pick a legal ISA + privilege level.  The chain order IS the strict-
// default stance progression: Pure pins NoneAllowed; production admits
// Vectorizable; bench admits NonDeterministicTsc (with CpuPinProof);
// warden::Hardening admits PrivilegedMsr only inside Init-ctx.
//
// Negative coverage (the chain's load-bearing TypeSafe guarantees) lives
// in test/safety_neg/neg_hw_instruction_*.cpp — two distinct mismatch
// classes proving the strong enum + the At<> singleton grade cannot be
// silently mixed with a sibling lattice.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;

namespace {

using cal::HwInstruction;
using L = cal::HwInstructionLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-251: HwInstructionLattice must satisfy the Lattice concept.");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-251: HwInstructionLattice has both bottom() (NoneAllowed) and "
    "top() (PrivilegedMsr).");
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-251: a chain order carries no independent ⊕/⊗ — the 'Tier-S "
    "Semiring' classification is the AXIS tier (V-253), not a Semiring "
    "concept on the lattice itself.");

// ── Cardinality — 5 tiers ───────────────────────────────────────────
static_assert(cal::detail::hw_instruction_lattice_self_test::hw_instruction_count == 5,
    "FIXY-V-251: HwInstruction must have exactly 5 enumerators. Adding a "
    "tier requires append-only ordinal placement + name switches + the "
    "V-254 Hw<> wrapper row_hash + V-260 collision rules.");

static_assert(std::is_same_v<std::underlying_type_t<HwInstruction>, std::uint8_t>,
    "FIXY-V-251: HwInstruction must use uint8_t underlying type — "
    "ChainLatticeOps derives leq via std::to_underlying.");

// ── Ordinal progression (the strict-default stance ladder) ──────────
static_assert(std::to_underlying(HwInstruction::NoneAllowed)         == 0, "bottom — stance::PureLinear default");
static_assert(std::to_underlying(HwInstruction::Scalar)              == 1);
static_assert(std::to_underlying(HwInstruction::Vectorizable)        == 2);
static_assert(std::to_underlying(HwInstruction::NonDeterministicTsc) == 3);
static_assert(std::to_underlying(HwInstruction::PrivilegedMsr)       == 4, "top — ring-0 MSR/port I/O");

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == HwInstruction::NoneAllowed);
static_assert(L::top()    == HwInstruction::PrivilegedMsr);

// ── Chain subsumption (every tier admits everything below) ──────────
static_assert(L::leq(HwInstruction::NoneAllowed,         HwInstruction::Scalar));
static_assert(L::leq(HwInstruction::Scalar,              HwInstruction::Vectorizable));
static_assert(L::leq(HwInstruction::Vectorizable,        HwInstruction::NonDeterministicTsc));
static_assert(L::leq(HwInstruction::NonDeterministicTsc, HwInstruction::PrivilegedMsr));
static_assert(L::leq(HwInstruction::NoneAllowed,         HwInstruction::PrivilegedMsr), "transitive endpoints");

// The load-bearing admission witnesses:
static_assert(L::leq(HwInstruction::Scalar, HwInstruction::NonDeterministicTsc),
    "FIXY-V-251: a Scalar kernel IS admitted by a NonDeterministicTsc "
    "execution context (capability is cumulative).");
static_assert(!L::leq(HwInstruction::NonDeterministicTsc, HwInstruction::Vectorizable),
    "FIXY-V-251: an rdtsc kernel is NOT admitted where only Vectorizable "
    "is allowed — descending the chain is rejected.");
static_assert(!L::leq(HwInstruction::PrivilegedMsr, HwInstruction::NoneAllowed),
    "FIXY-V-251: a ring-0 MSR kernel is NEVER admitted on a no-hw context.");

// ── Join — par=join (wider-instruction-class-dominates) ─────────────
static_assert(L::join(HwInstruction::Vectorizable, HwInstruction::NonDeterministicTsc)
              == HwInstruction::NonDeterministicTsc,
    "FIXY-V-251: composing a SIMD site with an rdtsc site yields the "
    "rdtsc tier — the region as a whole reads the TSC.");
static_assert(L::join(HwInstruction::NoneAllowed, HwInstruction::Scalar)
              == HwInstruction::Scalar, "NoneAllowed is the join identity");
static_assert(L::join(HwInstruction::PrivilegedMsr, HwInstruction::Scalar)
              == HwInstruction::PrivilegedMsr, "PrivilegedMsr absorbs in join");

// ── Meet — and=meet (tighter-instruction-floor) ────────────────────
static_assert(L::meet(HwInstruction::PrivilegedMsr, HwInstruction::Scalar)
              == HwInstruction::Scalar,
    "FIXY-V-251: meeting a permissive binding with a tight admission "
    "policy yields the tight floor.");
static_assert(L::meet(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr)
              == HwInstruction::NoneAllowed, "NoneAllowed absorbs in meet");

// ── At<T> singletons — empty element_type, EBO collapse, tier pin ───
static_assert(crucible::algebra::Lattice<L::At<HwInstruction::Scalar>>);
static_assert(crucible::algebra::BoundedLattice<L::At<HwInstruction::PrivilegedMsr>>);
static_assert(std::is_empty_v<L::At<HwInstruction::NoneAllowed>::element_type>,
    "FIXY-V-251: At<NoneAllowed>::element_type must be empty so "
    "Graded<Absolute, At<NoneAllowed>, P> EBO-collapses to sizeof(P).");
static_assert(std::is_empty_v<L::At<HwInstruction::Vectorizable>::element_type>);
static_assert(std::is_empty_v<L::At<HwInstruction::PrivilegedMsr>::element_type>);
static_assert(L::At<HwInstruction::Vectorizable>::tier == HwInstruction::Vectorizable,
    "FIXY-V-251: At<I>::tier must equal I at the type level so the V-254 "
    "wrapper reads the pinned tier with no runtime data.");

// ── EBO collapse witness — Graded<Absolute, At<Tier>, P> == sizeof(P) ─
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     L::At<HwInstruction::Vectorizable>, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-251: regime-1 EBO collapse — pinning a Vectorizable tier adds "
    "zero bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     L::At<HwInstruction::PrivilegedMsr>, int>)
    == sizeof(int));

// ── Names ───────────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"HwInstructionLattice"});
static_assert(L::At<HwInstruction::Scalar>::name()
              == std::string_view{"HwInstructionLattice::At<Scalar>"});
static_assert(L::At<HwInstruction::NonDeterministicTsc>::name()
              == std::string_view{"HwInstructionLattice::At<NonDeterministicTsc>"});
static_assert(cal::hw_instruction_name(HwInstruction::PrivilegedMsr)
              == std::string_view{"PrivilegedMsr"});

}  // namespace

int main() {
    cal::detail::hw_instruction_lattice_self_test
        ::hw_instruction_lattice_runtime_smoke_test();
    return 0;
}
