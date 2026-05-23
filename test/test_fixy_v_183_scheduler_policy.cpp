// FIXY-V-183 sentinel TU: algebra/lattices/SchedulerPolicyLattice.h —
// six-element total-order chain over the Linux scheduler-class spectrum
// ordered by preemption-aggressiveness
// (Idle ⊑ Batch ⊑ Other ⊑ RoundRobin ⊑ Fifo ⊑ Deadline).
//
// V-183 ships ONLY the lattice (the algebraic substrate): the enum +
// leq/join/meet (inherited from ChainLatticeOps) + At<P> singletons +
// reflection-driven cardinality/name-coverage + exhaustive (6³ = 216)
// lattice-axiom + distributivity verification + per-element order
// witnesses + the CtxFitsTscReader Other-threshold witnesses.  It ships
// NO value-level machinery and NO DimensionAxis enumerator:
//   - FIXY-V-186 ships safety/SchedClass.h (the Graded carrier; its
//     Deadline specialization carries <RuntimeNs, DeadlineNs, PeriodNs>
//     and checks runtime ≤ deadline at compile time) PLUS the
//     row_hash_contribution<SchedClass<...>> federation-cache
//     discriminator — exactly mirroring MemoryScopeLattice (V-265) →
//     safety/ScopedFence.h (V-267) + safety/diag/RowHashFold.h.  The
//     lattice layer pulls NO safety/diag header.
//   - The enumerator NAMES match warden/Policy.h's SchedClass set so the
//     V-186 bridge is mechanical; the ORDINAL (preemption rank) is this
//     lattice's own and is NOT the SCHED_* syscall constant.
//
// THE LOAD-BEARING PROPERTY this TU defends: Idle ⊑ ... ⊑ Deadline is a
// strict total chain, the descending direction is FALSE, and the
// CtxFitsTscReader threshold (leq(Other, P)) admits exactly
// Other/RoundRobin/Fifo/Deadline and rejects Idle/Batch.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/SchedulerPolicyLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace sp  = ::crucible::algebra::lattices::scheduler_policy;

namespace {

using cal::SchedulerPolicy;
using L = cal::SchedulerPolicyLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-183: SchedulerPolicyLattice must satisfy the Lattice concept "
    "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-183: SchedulerPolicyLattice has both bottom() (Idle) and "
    "top() (Deadline) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-183: SchedulerPolicyLattice is NOT a semiring — it carries no "
    "equality+add+mul algebra, only the order-theoretic operations.");

// ── Cardinality — exactly 6 classes ─────────────────────────────────
static_assert(cal::scheduler_policy_count == 6,
    "FIXY-V-183: SchedulerPolicy must have exactly 6 enumerators "
    "{Idle, Batch, Other, RoundRobin, Fifo, Deadline}.  Adding one "
    "requires (a) placing it at the correct ordinal so integer order "
    "equals preemption-aggressiveness, (b) extending both name switches, "
    "AND (c) bumping the V-186 wrapper satisfies<> + CtxFitsTscReader.");

static_assert(std::is_same_v<std::underlying_type_t<SchedulerPolicy>, std::uint8_t>,
    "FIXY-V-183: SchedulerPolicy must use uint8_t underlying type — the "
    "ordinal IS the preemption rank (Idle=0 ... Deadline=5), NOT the "
    "SCHED_* syscall constant value.");

// ── Ordinal convention — bottom=0, top=5 (preemption rank) ──────────
static_assert(std::to_underlying(SchedulerPolicy::Idle)       == 0);
static_assert(std::to_underlying(SchedulerPolicy::Batch)      == 1);
static_assert(std::to_underlying(SchedulerPolicy::Other)      == 2);
static_assert(std::to_underlying(SchedulerPolicy::RoundRobin) == 3);
static_assert(std::to_underlying(SchedulerPolicy::Fifo)       == 4);
static_assert(std::to_underlying(SchedulerPolicy::Deadline)   == 5);

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == SchedulerPolicy::Idle);
static_assert(L::top()    == SchedulerPolicy::Deadline);

// ── The chain (total order), one adjacency per element ──────────────
static_assert(L::leq(SchedulerPolicy::Idle,       SchedulerPolicy::Batch));
static_assert(L::leq(SchedulerPolicy::Batch,      SchedulerPolicy::Other));
static_assert(L::leq(SchedulerPolicy::Other,      SchedulerPolicy::RoundRobin));
static_assert(L::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo));
static_assert(L::leq(SchedulerPolicy::Fifo,       SchedulerPolicy::Deadline));
static_assert(L::leq(SchedulerPolicy::Idle,       SchedulerPolicy::Deadline), "transitive endpoints");

// ── THE LOAD-BEARING PROPERTY — descending direction is FALSE ───────
static_assert(!L::leq(SchedulerPolicy::Fifo,     SchedulerPolicy::RoundRobin),
    "FIXY-V-183: RoundRobin ⋣ Fifo — a round-robin thread does NOT "
    "satisfy a FIFO requirement.");
static_assert(!L::leq(SchedulerPolicy::Deadline, SchedulerPolicy::Fifo));
static_assert(!L::leq(SchedulerPolicy::Other,    SchedulerPolicy::Idle));
static_assert(!L::leq(SchedulerPolicy::Batch,    SchedulerPolicy::Idle));

// ── CtxFitsTscReader threshold (Agent 6 §3.2) — at least Other ──────
static_assert( L::leq(SchedulerPolicy::Other, SchedulerPolicy::Other));
static_assert( L::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert( L::leq(SchedulerPolicy::Other, SchedulerPolicy::Fifo));
static_assert( L::leq(SchedulerPolicy::Other, SchedulerPolicy::Deadline));
static_assert(!L::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
    "CtxFitsTscReader: SCHED_BATCH is below the Other TSC-read threshold.");
static_assert(!L::leq(SchedulerPolicy::Other, SchedulerPolicy::Idle));

// ── Join strengthens (max); meet weakens (min) ──────────────────────
static_assert(L::join(SchedulerPolicy::Idle, SchedulerPolicy::Deadline)     == SchedulerPolicy::Deadline);
static_assert(L::join(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)   == SchedulerPolicy::Fifo);
static_assert(L::join(SchedulerPolicy::Idle, SchedulerPolicy::Batch)        == SchedulerPolicy::Batch,
    "Idle is the join identity");
static_assert(L::meet(SchedulerPolicy::Idle, SchedulerPolicy::Deadline)     == SchedulerPolicy::Idle,
    "Idle absorbs in meet");
static_assert(L::meet(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)   == SchedulerPolicy::RoundRobin);
static_assert(L::meet(SchedulerPolicy::Deadline, SchedulerPolicy::Deadline) == SchedulerPolicy::Deadline);

// ── At<P> singleton sub-lattice — empty element_type, EBO collapse ──
static_assert(crucible::algebra::Lattice<sp::IdleClass>);
static_assert(crucible::algebra::Lattice<sp::FifoClass>);
static_assert(crucible::algebra::BoundedLattice<sp::DeadlineClass>);
static_assert(std::is_empty_v<sp::IdleClass::element_type>,
    "FIXY-V-183: At<Idle>::element_type must be empty so "
    "Graded<Absolute, At<Idle>, P> EBO-collapses to sizeof(P) — a "
    "zero-byte scheduler-class annotation at every binding site.");
static_assert(std::is_empty_v<sp::OtherClass::element_type>);
static_assert(std::is_empty_v<sp::FifoClass::element_type>);
static_assert(std::is_empty_v<sp::DeadlineClass::element_type>);
static_assert(sp::FifoClass::policy == SchedulerPolicy::Fifo,
    "FIXY-V-183: At<P>::policy must equal P at the type level so the "
    "V-186 wrapper reads the pinned policy with no runtime data.");
static_assert(sp::RoundRobinClass::policy == SchedulerPolicy::RoundRobin);
static_assert(sp::IdleClass::policy       == SchedulerPolicy::Idle);
static_assert(sp::DeadlineClass::policy   == SchedulerPolicy::Deadline);

// ── EBO collapse witness — Graded<Absolute, At<P>, V> == sizeof(V) ──
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     sp::FifoClass, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-183: regime-1 EBO collapse — pinning a Fifo grade adds zero "
    "bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     sp::IdleClass, int>)
    == sizeof(int));

// ── Name surface ────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"SchedulerPolicyLattice"});
static_assert(sp::FifoClass::name()       == std::string_view{"SchedulerPolicyLattice::At<Fifo>"});
static_assert(sp::RoundRobinClass::name() == std::string_view{"SchedulerPolicyLattice::At<RoundRobin>"});
static_assert(sp::DeadlineClass::name()   == std::string_view{"SchedulerPolicyLattice::At<Deadline>"});
static_assert(sp::IdleClass::name()       == std::string_view{"SchedulerPolicyLattice::At<Idle>"});
static_assert(cal::scheduler_policy_name(SchedulerPolicy::RoundRobin) == std::string_view{"RoundRobin"});

}  // namespace

int main() {
    cal::detail::scheduler_policy_lattice_self_test::runtime_smoke_test();
    return 0;
}
