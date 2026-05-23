// FIXY-V-181 sentinel TU: algebra/lattices/SuspendBehaviorLattice.h —
// three-element total-order chain over the clock-PAUSE-on-suspend
// spectrum (Unknown ⊑ PausesOnSuspend ⊑ KeepsTicking).
//
// V-181 ships ONLY the lattice (the algebraic substrate): the enum +
// leq/join/meet (inherited from ChainLatticeOps) + At<B> singletons +
// reflection-driven cardinality/name-coverage + exhaustive (3³ = 27)
// lattice-axiom + distributivity verification.  It ships NO value-level
// machinery and NO DimensionAxis enumerator:
//   - the SuspendBehavior axis enumerator + DimensionTraits wiring land
//     with the V-188 wrapper epoch, so this TU does NOT reference
//     safety/DimensionTraits.h.
//   - FIXY-V-188 ships safety/SuspendBehavior.h (the Graded<Absolute,
//     SuspendBehaviorLattice::At<B>, T> carrier) PLUS the `satisfies<>`
//     gate AND the row_hash_contribution<SuspendBehavior<...>>
//     federation-cache discriminator — exactly mirroring how
//     MemoryScopeLattice (V-265) defers its wrapper + row_hash to
//     safety/ScopedFence.h (V-267) + safety/diag/RowHashFold.h.  The
//     lattice layer pulls NO safety/diag header.
//   - FIXY-V-184 composes this lattice (with DetSafe + Pinning) into
//     ClockSourceLattice via ProductLattice.h; FIXY-V-194 threads the
//     KeepsTicking requirement through warden/DeadlineWatchdog.
//
// THE LOAD-BEARING PROPERTY this TU defends: KeepsTicking ⊒
// PausesOnSuspend ⊒ Unknown is a strict total chain, and the descending
// direction is FALSE.  The V-188/V-194 safety guarantee ("a deadline
// consumer that requires suspend-inclusive elapsed is NEVER satisfied by
// a CLOCK_MONOTONIC provider") reduces directly to `!leq(KeepsTicking,
// PausesOnSuspend)` — the type-level form of the DeadlineWatchdog
// false-Healthy-after-suspend bug.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace sb  = ::crucible::algebra::lattices::suspend_behavior;

namespace {

using cal::SuspendBehavior;
using L = cal::SuspendBehaviorLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-181: SuspendBehaviorLattice must satisfy the Lattice concept "
    "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-181: SuspendBehaviorLattice has both bottom() (Unknown) and "
    "top() (KeepsTicking) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-181: SuspendBehaviorLattice is NOT a semiring — it carries no "
    "equality+add+mul algebra, only the order-theoretic operations.");

// ── Cardinality — exactly 3 behaviors ───────────────────────────────
static_assert(cal::suspend_behavior_count == 3,
    "FIXY-V-181: SuspendBehavior must have exactly 3 enumerators "
    "{Unknown, PausesOnSuspend, KeepsTicking}.  Adding one requires "
    "(a) placing it at the correct ordinal so integer order equals "
    "suspend-resilience rank, (b) extending both name switches, AND "
    "(c) bumping the V-184 composition + V-188 wrapper satisfies<>.");

static_assert(std::is_same_v<std::underlying_type_t<SuspendBehavior>, std::uint8_t>,
    "FIXY-V-181: SuspendBehavior must use uint8_t underlying type — the "
    "ordinal IS the suspend-resilience rank (Unknown=0 ... KeepsTicking=2).");

// ── Ordinal convention — bottom=0, top=2 ────────────────────────────
static_assert(std::to_underlying(SuspendBehavior::Unknown)         == 0);
static_assert(std::to_underlying(SuspendBehavior::PausesOnSuspend) == 1);
static_assert(std::to_underlying(SuspendBehavior::KeepsTicking)    == 2);

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == SuspendBehavior::Unknown);
static_assert(L::top()    == SuspendBehavior::KeepsTicking);

// ── The chain (total order) — Unknown ⊑ PausesOnSuspend ⊑ KeepsTicking
static_assert(L::leq(SuspendBehavior::Unknown,         SuspendBehavior::PausesOnSuspend));
static_assert(L::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking));
static_assert(L::leq(SuspendBehavior::Unknown,         SuspendBehavior::KeepsTicking), "transitive endpoints");

// ── THE LOAD-BEARING PROPERTY — descending direction is FALSE ───────
//
// A CLOCK_MONOTONIC (PausesOnSuspend) provider must NEVER satisfy a
// CLOCK_BOOTTIME (KeepsTicking) requirement; that mismatch IS the
// DeadlineWatchdog false-Healthy-after-suspend bug, forbidden here at
// the type level.
static_assert(!L::leq(SuspendBehavior::KeepsTicking,    SuspendBehavior::PausesOnSuspend),
    "FIXY-V-181: PausesOnSuspend ⋣ KeepsTicking — a monotonic clock does "
    "NOT satisfy a suspend-inclusive requirement.");
static_assert(!L::leq(SuspendBehavior::KeepsTicking,    SuspendBehavior::Unknown));
static_assert(!L::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::Unknown));

// ── Join — the more-suspend-resilient (max) ─────────────────────────
static_assert(L::join(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking)    == SuspendBehavior::KeepsTicking);
static_assert(L::join(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking) == SuspendBehavior::KeepsTicking);
static_assert(L::join(SuspendBehavior::Unknown, SuspendBehavior::PausesOnSuspend) == SuspendBehavior::PausesOnSuspend,
    "Unknown is the join identity");
static_assert(L::join(SuspendBehavior::KeepsTicking, SuspendBehavior::Unknown)    == SuspendBehavior::KeepsTicking,
    "KeepsTicking absorbs in join");

// ── Meet — the less-suspend-resilient (min) ─────────────────────────
static_assert(L::meet(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking)    == SuspendBehavior::Unknown,
    "Unknown absorbs in meet");
static_assert(L::meet(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking) == SuspendBehavior::PausesOnSuspend);
static_assert(L::meet(SuspendBehavior::KeepsTicking, SuspendBehavior::KeepsTicking) == SuspendBehavior::KeepsTicking,
    "KeepsTicking is the meet identity at the top");

// ── At<B> singleton sub-lattice — empty element_type, EBO collapse ──
static_assert(crucible::algebra::Lattice<sb::UnknownBehavior>);
static_assert(crucible::algebra::Lattice<sb::PausesOnSuspendClock>);
static_assert(crucible::algebra::BoundedLattice<sb::KeepsTickingClock>);
static_assert(std::is_empty_v<sb::UnknownBehavior::element_type>,
    "FIXY-V-181: At<Unknown>::element_type must be empty so "
    "Graded<Absolute, At<Unknown>, P> EBO-collapses to sizeof(P) — a "
    "zero-byte suspend-behavior annotation at every binding site.");
static_assert(std::is_empty_v<sb::PausesOnSuspendClock::element_type>);
static_assert(std::is_empty_v<sb::KeepsTickingClock::element_type>);
static_assert(sb::KeepsTickingClock::behavior == SuspendBehavior::KeepsTicking,
    "FIXY-V-181: At<B>::behavior must equal B at the type level so the "
    "V-188 wrapper reads the pinned behavior with no runtime data.");
static_assert(sb::PausesOnSuspendClock::behavior == SuspendBehavior::PausesOnSuspend);
static_assert(sb::UnknownBehavior::behavior      == SuspendBehavior::Unknown);

// ── EBO collapse witness — Graded<Absolute, At<B>, P> == sizeof(P) ──
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     sb::KeepsTickingClock, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-181: regime-1 EBO collapse — pinning a KeepsTicking grade adds "
    "zero bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     sb::UnknownBehavior, int>)
    == sizeof(int));

// ── Name surface ────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"SuspendBehaviorLattice"});
static_assert(sb::KeepsTickingClock::name()    == std::string_view{"SuspendBehaviorLattice::At<KeepsTicking>"});
static_assert(sb::PausesOnSuspendClock::name() == std::string_view{"SuspendBehaviorLattice::At<PausesOnSuspend>"});
static_assert(sb::UnknownBehavior::name()      == std::string_view{"SuspendBehaviorLattice::At<Unknown>"});
static_assert(cal::suspend_behavior_name(SuspendBehavior::KeepsTicking) == std::string_view{"KeepsTicking"});

}  // namespace

int main() {
    cal::detail::suspend_behavior_lattice_self_test::runtime_smoke_test();
    return 0;
}
