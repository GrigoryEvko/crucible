// FIXY-V-182 sentinel TU: algebra/lattices/PinningRequirementLattice.h —
// four-element total-order chain over the CPU-coherence-domain spectrum
// (NotRequired ⊑ PerCore ⊑ PerSocket ⊑ CrossSocketSafe).
//
// V-182 ships ONLY the lattice (the algebraic substrate): the enum +
// leq/join/meet (inherited from ChainLatticeOps) + At<P> singletons +
// reflection-driven cardinality/name-coverage + exhaustive (4³ = 64)
// lattice-axiom + distributivity verification.  It ships NO value-level
// machinery and NO DimensionAxis enumerator:
//   - the value-level wiring lands with the V-187 wrapper epoch, so this
//     TU does NOT reference safety/DimensionTraits.h.
//   - FIXY-V-187 ships safety/CpuPinned.h (the Graded carrier composing
//     this with the existing AffinityLattice) PLUS the
//     row_hash_contribution<CpuPinned<...>> federation-cache
//     discriminator — exactly mirroring how MemoryScopeLattice (V-265)
//     defers its wrapper + row_hash to safety/ScopedFence.h (V-267) +
//     safety/diag/RowHashFold.h.  The lattice layer pulls NO safety/diag
//     header.
//   - FIXY-V-184 composes this lattice (with DetSafe + SuspendBehavior)
//     into ClockSourceLattice via ProductLattice.h; FIXY-V-196 threads a
//     PerCore requirement through bench/bench_harness.h's rdtsc path.
//
// THE LOAD-BEARING PROPERTY this TU defends: NotRequired ⊑ PerCore ⊑
// PerSocket ⊑ CrossSocketSafe is a strict total chain, and the
// descending direction is FALSE.  The V-187/V-196 safety guarantee ("an
// rdtsc read that requires PerSocket coherence is NEVER satisfied by a
// merely PerCore-pinned source") reduces directly to `!leq(PerSocket,
// PerCore)` — the type-level form of the cross-CCD negative-delta bug.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/PinningRequirementLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace pr  = ::crucible::algebra::lattices::pinning_requirement;

namespace {

using cal::PinningRequirement;
using L = cal::PinningRequirementLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-182: PinningRequirementLattice must satisfy the Lattice "
    "concept (element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-182: PinningRequirementLattice has both bottom() (NotRequired) "
    "and top() (CrossSocketSafe) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-182: PinningRequirementLattice is NOT a semiring — it carries "
    "no equality+add+mul algebra, only the order-theoretic operations.");

// ── Cardinality — exactly 4 levels ──────────────────────────────────
static_assert(cal::pinning_requirement_count == 4,
    "FIXY-V-182: PinningRequirement must have exactly 4 enumerators "
    "{NotRequired, PerCore, PerSocket, CrossSocketSafe}.  Adding one "
    "requires (a) placing it at the correct ordinal so integer order "
    "equals coherence-domain breadth, (b) extending both name switches, "
    "AND (c) bumping the V-184 composition + V-187 wrapper satisfies<>.");

static_assert(std::is_same_v<std::underlying_type_t<PinningRequirement>, std::uint8_t>,
    "FIXY-V-182: PinningRequirement must use uint8_t underlying type — the "
    "ordinal IS the coherence-domain rank (NotRequired=0 ... "
    "CrossSocketSafe=3).");

// ── Ordinal convention — bottom=0, top=3 ────────────────────────────
static_assert(std::to_underlying(PinningRequirement::NotRequired)     == 0);
static_assert(std::to_underlying(PinningRequirement::PerCore)         == 1);
static_assert(std::to_underlying(PinningRequirement::PerSocket)       == 2);
static_assert(std::to_underlying(PinningRequirement::CrossSocketSafe) == 3);

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == PinningRequirement::NotRequired);
static_assert(L::top()    == PinningRequirement::CrossSocketSafe);

// ── The chain (total order) — NotRequired ⊑ ... ⊑ CrossSocketSafe ────
static_assert(L::leq(PinningRequirement::NotRequired, PinningRequirement::PerCore));
static_assert(L::leq(PinningRequirement::PerCore,     PinningRequirement::PerSocket));
static_assert(L::leq(PinningRequirement::PerSocket,   PinningRequirement::CrossSocketSafe));
static_assert(L::leq(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe), "transitive endpoints");

// ── THE LOAD-BEARING PROPERTY — descending direction is FALSE ───────
//
// A merely PerCore-coherent source must NEVER satisfy a PerSocket (or
// wider) requirement; that mismatch IS the rdtsc cross-CCD/cross-socket
// negative-delta bug, forbidden here at the type level.
static_assert(!L::leq(PinningRequirement::PerSocket,       PinningRequirement::PerCore),
    "FIXY-V-182: PerCore ⋣ PerSocket — a per-core-coherent source does "
    "NOT satisfy a socket-wide requirement.");
static_assert(!L::leq(PinningRequirement::CrossSocketSafe, PinningRequirement::PerSocket));
static_assert(!L::leq(PinningRequirement::PerCore,         PinningRequirement::NotRequired));

// ── Join — the wider coherence domain (max) ─────────────────────────
static_assert(L::join(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe) == PinningRequirement::CrossSocketSafe);
static_assert(L::join(PinningRequirement::PerCore, PinningRequirement::PerSocket)           == PinningRequirement::PerSocket);
static_assert(L::join(PinningRequirement::NotRequired, PinningRequirement::PerCore)         == PinningRequirement::PerCore,
    "NotRequired is the join identity");
static_assert(L::join(PinningRequirement::CrossSocketSafe, PinningRequirement::PerCore)     == PinningRequirement::CrossSocketSafe,
    "CrossSocketSafe absorbs in join");

// ── Meet — the narrower coherence domain (min) ──────────────────────
static_assert(L::meet(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe) == PinningRequirement::NotRequired,
    "NotRequired absorbs in meet");
static_assert(L::meet(PinningRequirement::PerCore, PinningRequirement::PerSocket)           == PinningRequirement::PerCore);
static_assert(L::meet(PinningRequirement::CrossSocketSafe, PinningRequirement::CrossSocketSafe) == PinningRequirement::CrossSocketSafe,
    "CrossSocketSafe is the meet identity at the top");

// ── At<P> singleton sub-lattice — empty element_type, EBO collapse ──
static_assert(crucible::algebra::Lattice<pr::NotRequiredPin>);
static_assert(crucible::algebra::Lattice<pr::PerCorePin>);
static_assert(crucible::algebra::BoundedLattice<pr::CrossSocketSafePin>);
static_assert(std::is_empty_v<pr::NotRequiredPin::element_type>,
    "FIXY-V-182: At<NotRequired>::element_type must be empty so "
    "Graded<Absolute, At<NotRequired>, P> EBO-collapses to sizeof(P) — a "
    "zero-byte pinning-requirement annotation at every binding site.");
static_assert(std::is_empty_v<pr::PerCorePin::element_type>);
static_assert(std::is_empty_v<pr::PerSocketPin::element_type>);
static_assert(std::is_empty_v<pr::CrossSocketSafePin::element_type>);
static_assert(pr::PerCorePin::requirement == PinningRequirement::PerCore,
    "FIXY-V-182: At<P>::requirement must equal P at the type level so the "
    "V-187 wrapper reads the pinned requirement with no runtime data.");
static_assert(pr::PerSocketPin::requirement       == PinningRequirement::PerSocket);
static_assert(pr::NotRequiredPin::requirement     == PinningRequirement::NotRequired);
static_assert(pr::CrossSocketSafePin::requirement == PinningRequirement::CrossSocketSafe);

// ── EBO collapse witness — Graded<Absolute, At<P>, V> == sizeof(V) ──
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     pr::PerCorePin, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-182: regime-1 EBO collapse — pinning a PerCore grade adds zero "
    "bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     pr::NotRequiredPin, int>)
    == sizeof(int));

// ── Name surface ────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"PinningRequirementLattice"});
static_assert(pr::PerCorePin::name()         == std::string_view{"PinningRequirementLattice::At<PerCore>"});
static_assert(pr::PerSocketPin::name()       == std::string_view{"PinningRequirementLattice::At<PerSocket>"});
static_assert(pr::CrossSocketSafePin::name() == std::string_view{"PinningRequirementLattice::At<CrossSocketSafe>"});
static_assert(pr::NotRequiredPin::name()     == std::string_view{"PinningRequirementLattice::At<NotRequired>"});
static_assert(cal::pinning_requirement_name(PinningRequirement::PerSocket) == std::string_view{"PerSocket"});

}  // namespace

int main() {
    cal::detail::pinning_requirement_lattice_self_test::runtime_smoke_test();
    return 0;
}
