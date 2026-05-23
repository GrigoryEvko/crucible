#pragma once

// ── crucible::algebra::lattices::SchedulerPolicyLattice ─────────────
//
// Six-element total-order chain lattice over the Linux scheduler-class
// spectrum, ordered by PREEMPTION-AGGRESSIVENESS (Agent 6 §3.2).
//
// ── The classification ──────────────────────────────────────────────
//
// Each element names a Linux scheduling policy (set via sched_setattr).
// They are listed bottom-to-top by how aggressively a thread in that
// class preempts / is scheduled over the default CFS pool.
//
//     Idle        — SCHED_IDLE: lowest; runs only when the CPU is
//                   otherwise idle.  Bottom of the chain.
//     Batch       — SCHED_BATCH: throughput-oriented, deliberately less
//                   preemption than SCHED_OTHER (no interactive boost).
//     Other       — SCHED_OTHER: the default time-shared CFS class.
//                   The TSC-read threshold (see CtxFitsTscReader below).
//     RoundRobin  — SCHED_RR: real-time, time-sliced among same-priority
//                   threads.
//     Fifo        — SCHED_FIFO: real-time, runs until it yields/blocks —
//                   starves equal-or-lower priority (more aggressive than
//                   RR's time-slice).
//     Deadline    — SCHED_DEADLINE: CBS-admitted EDF (runtime, deadline,
//                   period); preempts FIFO/RR.  Top of the chain — the
//                   highest scheduling priority over CFS.
//
// ── Three distinct numberings — do NOT conflate ─────────────────────
//
// (a) Linux syscall CONSTANTS: SCHED_OTHER=0, SCHED_FIFO=1, SCHED_RR=2,
//     SCHED_BATCH=3, SCHED_IDLE=5, SCHED_DEADLINE=6.
// (b) warden/Policy.h's SchedClass DECLARATION order (Other=0 ...
//     Deadline=5) — a convenience enum for the hardening syscall, NOT a
//     preemption order.
// (c) THIS lattice's ordinal = preemption rank (Idle=0 ... Deadline=5).
//
// Only (c) makes `leq` mean "weaker-preemption consumer is satisfied by
// a stronger provider."  The V-186 safety/SchedClass.h wrapper
// translates (c) → (a) at the sched_setattr boundary.  Enumerator NAMES
// match warden::SchedClass's set ({Idle, Batch, Other, RoundRobin, Fifo,
// Deadline}) so the V-186 bridge is mechanical; the ORDINAL is this
// lattice's own.  algebra/lattices/ is foundational and does NOT depend
// on warden/ — this is a fresh enum, not a reuse of warden::SchedClass.
//
// Macro hygiene: `SCHED_FIFO` etc. are <sched.h> `#define`s, so the
// enumerators are PascalCase (`Fifo`, `Idle`, ...) — macro-immune.  The
// kernel spellings appear only inside string literals (the preprocessor
// never expands string contents).
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class SchedulerPolicy ∈ {Idle, Batch, Other, RoundRobin,
//          Fifo, Deadline}.
// Order:   Idle ⊑ Batch ⊑ Other ⊑ RoundRobin ⊑ Fifo ⊑ Deadline.
//
// Bottom = Idle      (weakest; satisfies only an Idle-tolerating consumer).
// Top    = Deadline  (strongest; CBS-admitted EDF — satisfies every
//                     consumer).
// Join   = max       (the more-aggressive of two providers).
// Meet   = min       (the less-aggressive).
//
// ── Direction convention (matches DetSafe / SuspendBehavior / Pinning) ─
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)` reads
// "a weaker-preemption requirement is satisfied by a stronger-preemption
// provider" — a Fifo thread serves a consumer that only required
// RoundRobin or Other.  Crucible-standard subsumption-up, shared with
// DetSafeLattice (Pure = top), SuspendBehaviorLattice (KeepsTicking =
// top), PinningRequirementLattice (CrossSocketSafe = top).
//
// ── Cross-axis consumer: CtxFitsTscReader (Agent 6 §3.2) ────────────
//
// The TSC-reader concept requires the scheduling policy be AT LEAST
// `Other` (`leq(Other, P)`).  Under Idle/Batch the kernel may migrate a
// thread even mid-quantum, so a `sched_getcpu()`-based "pin" gives no
// migration guarantee — the TSC read needs Other-or-stronger scheduling
// (RoundRobin/Fifo/Deadline) OR an explicit singleton-mask affinity
// proof.  The lattice supplies the `leq(Other, P)` test that concept
// reduces to.
//
// ── This header pulls NO row_hash machinery ─────────────────────────
//
// The row_hash key is the WRAPPER, never the lattice At<> — as
// BarrierStrengthLattice.h:130 / HwInstructionLattice.h:136 /
// MemoryScopeLattice.h / SuspendBehaviorLattice.h /
// PinningRequirementLattice.h document.  FIXY-V-186 ships
// safety/SchedClass.h (the Graded carrier; its Deadline specialization
// carries the <RuntimeNs, DeadlineNs, PeriodNs> NTTPs and checks
// runtime ≤ deadline at compile time) PLUS the
// row_hash_contribution<SchedClass<...>> federation-cache discriminator
// in safety/diag/RowHashFold.h, mirroring MemoryScopeLattice (V-265) →
// safety/ScopedFence.h (V-267).  `algebra/lattices/` sits BELOW
// `safety/diag/`; keying row_hash on the lattice At<> (a nested template,
// not forward-declarable) would invert that layering.
//
//   Axiom coverage:
//     TypeSafe — SchedulerPolicy is a strong scoped enum (`enum class :
//                uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare + select; the six-
//     element domain compiles to a 1-byte field with one branch.  When
//     wrapped at a fixed type-level policy via
//     `SchedulerPolicyLattice::At<Fifo>`, the grade EBO-collapses to zero
//     bytes.
//
// See Agent 6 §3.2 for the design rationale; FIXY-V-186 (safety/
// SchedClass.h) for the type-pinned wrapper + row_hash + DEADLINE NTTPs;
// warden/Policy.h SchedClass + warden/Hardening.h mint_hardening for the
// sched_setattr syscall surface this axis types.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── SchedulerPolicy — chain over the Linux scheduler-class spectrum ──
//
// Ordinal convention: Idle=0 (bottom) ... Deadline=5 (top), by
// preemption-aggressiveness — matching the DetSafe/SuspendBehavior/
// Pinning project convention (bottom=0, strongest at top).  NOTE: this
// ordinal is NOT the SCHED_* syscall constant value (see docblock).
enum class SchedulerPolicy : std::uint8_t {
    Idle       = 0,    // bottom: SCHED_IDLE — runs only when CPU idle
    Batch      = 1,    // SCHED_BATCH — throughput, less preemption
    Other      = 2,    // SCHED_OTHER — default CFS; the TSC-read threshold
    RoundRobin = 3,    // SCHED_RR — real-time, time-sliced
    Fifo       = 4,    // SCHED_FIFO — real-time, runs until yield/block
    Deadline   = 5,    // top: SCHED_DEADLINE — CBS-admitted EDF
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t scheduler_policy_count =
    std::meta::enumerators_of(^^SchedulerPolicy).size();

[[nodiscard]] consteval std::string_view
scheduler_policy_name(SchedulerPolicy p) noexcept {
    switch (p) {
        case SchedulerPolicy::Idle:       return "Idle";
        case SchedulerPolicy::Batch:      return "Batch";
        case SchedulerPolicy::Other:      return "Other";
        case SchedulerPolicy::RoundRobin: return "RoundRobin";
        case SchedulerPolicy::Fifo:       return "Fifo";
        case SchedulerPolicy::Deadline:   return "Deadline";
        default: return std::string_view{"<unknown SchedulerPolicy>"};
    }
}

// ── Full SchedulerPolicyLattice (chain order) ───────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<SchedulerPolicy> — see
// ChainLattice.h for the rationale.
struct SchedulerPolicyLattice : ChainLatticeOps<SchedulerPolicy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return SchedulerPolicy::Idle;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return SchedulerPolicy::Deadline;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "SchedulerPolicyLattice";
    }

    // ── At<P>: singleton sub-lattice at a fixed type-level policy ────
    //
    // Used by the V-186 SchedClass wrapper:
    //   using FifoThread =
    //       Graded<Absolute, SchedulerPolicyLattice::At<Fifo>, ...>;
    template <SchedulerPolicy P>
    struct At {
        struct element_type {
            using scheduler_policy_value_type = SchedulerPolicy;
            [[nodiscard]] constexpr operator scheduler_policy_value_type() const noexcept {
                return P;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr SchedulerPolicy policy = P;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (P) {
                case SchedulerPolicy::Idle:       return "SchedulerPolicyLattice::At<Idle>";
                case SchedulerPolicy::Batch:      return "SchedulerPolicyLattice::At<Batch>";
                case SchedulerPolicy::Other:      return "SchedulerPolicyLattice::At<Other>";
                case SchedulerPolicy::RoundRobin: return "SchedulerPolicyLattice::At<RoundRobin>";
                case SchedulerPolicy::Fifo:       return "SchedulerPolicyLattice::At<Fifo>";
                case SchedulerPolicy::Deadline:   return "SchedulerPolicyLattice::At<Deadline>";
                default:                          return "SchedulerPolicyLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace scheduler_policy {
    using IdleClass       = SchedulerPolicyLattice::At<SchedulerPolicy::Idle>;
    using BatchClass      = SchedulerPolicyLattice::At<SchedulerPolicy::Batch>;
    using OtherClass      = SchedulerPolicyLattice::At<SchedulerPolicy::Other>;
    using RoundRobinClass = SchedulerPolicyLattice::At<SchedulerPolicy::RoundRobin>;
    using FifoClass       = SchedulerPolicyLattice::At<SchedulerPolicy::Fifo>;
    using DeadlineClass   = SchedulerPolicyLattice::At<SchedulerPolicy::Deadline>;
}  // namespace scheduler_policy

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::scheduler_policy_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(scheduler_policy_count == 6,
    "SchedulerPolicy catalog diverged from {Idle, Batch, Other, "
    "RoundRobin, Fifo, Deadline}; adding a class requires extending both "
    "name switches AND bumping the V-186 SchedClass wrapper satisfies<> "
    "gate + the CtxFitsTscReader threshold.");

[[nodiscard]] consteval bool every_scheduler_policy_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SchedulerPolicy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (scheduler_policy_name([:en:]) ==
            std::string_view{"<unknown SchedulerPolicy>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_scheduler_policy_has_name(),
    "scheduler_policy_name() switch missing arm for at least one class — "
    "add the arm or the new class leaks the '<unknown SchedulerPolicy>' "
    "sentinel into observer debug output.");

// Concept conformance — full lattice + each At<P> sub-lattice.
static_assert(Lattice<SchedulerPolicyLattice>);
static_assert(BoundedLattice<SchedulerPolicyLattice>);
static_assert(Lattice<scheduler_policy::IdleClass>);
static_assert(Lattice<scheduler_policy::DeadlineClass>);
static_assert(BoundedLattice<scheduler_policy::DeadlineClass>);

// Negative concept assertions — pin SchedulerPolicyLattice's character.
static_assert(!UnboundedLattice<SchedulerPolicyLattice>);
static_assert(!Semiring<SchedulerPolicyLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<scheduler_policy::IdleClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::OtherClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::FifoClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::DeadlineClass::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (SchedulerPolicy)³ = 216 triples each.  Both verifiers extracted into
// ChainLattice.h — adding a new class auto-extends coverage.
static_assert(verify_chain_lattice_exhaustive<SchedulerPolicyLattice>(),
    "SchedulerPolicyLattice's chain-order lattice axioms must hold at "
    "every (SchedulerPolicy)³ triple — failure indicates a defect in "
    "leq/join/meet or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<SchedulerPolicyLattice>(),
    "SchedulerPolicyLattice's chain order must satisfy distributivity at "
    "every (SchedulerPolicy)³ triple — a chain order always does, so "
    "failure would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing, with Deadline
// at the top (most aggressive preemption).
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Idle,       SchedulerPolicy::Batch));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Batch,      SchedulerPolicy::Other));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Other,      SchedulerPolicy::RoundRobin));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo,       SchedulerPolicy::Deadline));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Idle,       SchedulerPolicy::Deadline));  // transitive endpoints
// The load-bearing subsumption: a stronger-preemption provider serves a
// weaker requirement; the descending direction is FALSE.
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo),
    "SchedulerPolicyLattice: Fifo ⊒ RoundRobin — a FIFO thread serves a "
    "round-robin requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo,       SchedulerPolicy::RoundRobin),
    "SchedulerPolicyLattice: RoundRobin ⋣ Fifo — a round-robin thread "
    "does NOT satisfy a FIFO requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Deadline,   SchedulerPolicy::Idle));

// CtxFitsTscReader threshold (Agent 6 §3.2): TSC reads require AT LEAST
// Other-strength scheduling.  Other/RoundRobin/Fifo/Deadline satisfy
// `leq(Other, P)`; Idle/Batch (below Other) do NOT.
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Other));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Fifo));
static_assert( SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Deadline));
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
    "CtxFitsTscReader: SCHED_BATCH is below the Other TSC-read threshold — "
    "the kernel may migrate mid-quantum, so a sched_getcpu pin is unsound.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Idle));

// Pin bottom / top to the chain endpoints.
static_assert(SchedulerPolicyLattice::bottom() == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::top()    == SchedulerPolicy::Deadline);

// Join strengthens (max); meet weakens (min).
static_assert(SchedulerPolicyLattice::join(SchedulerPolicy::Idle, SchedulerPolicy::Deadline)
              == SchedulerPolicy::Deadline);
static_assert(SchedulerPolicyLattice::join(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)
              == SchedulerPolicy::Fifo);
static_assert(SchedulerPolicyLattice::meet(SchedulerPolicy::Idle, SchedulerPolicy::Deadline)
              == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::meet(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)
              == SchedulerPolicy::RoundRobin);

// Diagnostic names.
static_assert(SchedulerPolicyLattice::name() == "SchedulerPolicyLattice");
static_assert(scheduler_policy::IdleClass::name()       == "SchedulerPolicyLattice::At<Idle>");
static_assert(scheduler_policy::BatchClass::name()      == "SchedulerPolicyLattice::At<Batch>");
static_assert(scheduler_policy::OtherClass::name()      == "SchedulerPolicyLattice::At<Other>");
static_assert(scheduler_policy::RoundRobinClass::name() == "SchedulerPolicyLattice::At<RoundRobin>");
static_assert(scheduler_policy::FifoClass::name()       == "SchedulerPolicyLattice::At<Fifo>");
static_assert(scheduler_policy::DeadlineClass::name()   == "SchedulerPolicyLattice::At<Deadline>");

// Reflection-driven coverage check on At<P>::name().
[[nodiscard]] consteval bool every_at_scheduler_policy_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SchedulerPolicy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (SchedulerPolicyLattice::At<([:en:])>::name() ==
            std::string_view{"SchedulerPolicyLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_scheduler_policy_has_name(),
    "SchedulerPolicyLattice::At<P>::name() switch missing an arm for at "
    "least one class — add the arm or the new class leaks the "
    "'SchedulerPolicyLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(scheduler_policy::IdleClass::policy       == SchedulerPolicy::Idle);
static_assert(scheduler_policy::BatchClass::policy      == SchedulerPolicy::Batch);
static_assert(scheduler_policy::OtherClass::policy      == SchedulerPolicy::Other);
static_assert(scheduler_policy::RoundRobinClass::policy == SchedulerPolicy::RoundRobin);
static_assert(scheduler_policy::FifoClass::policy       == SchedulerPolicy::Fifo);
static_assert(scheduler_policy::DeadlineClass::policy   == SchedulerPolicy::Deadline);

// ── Layout invariants on Graded<...,At<P>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// FifoClass — a real-time class typical of hot dispatch threads.
template <typename T_>
using FifoGraded = Graded<ModalityKind::Absolute, scheduler_policy::FifoClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, int);

// DeadlineClass — the CBS-admitted top.
template <typename T_>
using DeadlineGraded = Graded<ModalityKind::Absolute, scheduler_policy::DeadlineClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(DeadlineGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.
inline void runtime_smoke_test() {
    // Full SchedulerPolicyLattice ops at runtime.
    SchedulerPolicy a = SchedulerPolicy::Idle;
    SchedulerPolicy b = SchedulerPolicy::Deadline;
    [[maybe_unused]] bool            l1  = SchedulerPolicyLattice::leq(a, b);
    [[maybe_unused]] SchedulerPolicy j1  = SchedulerPolicyLattice::join(a, b);
    [[maybe_unused]] SchedulerPolicy m1  = SchedulerPolicyLattice::meet(a, b);
    [[maybe_unused]] SchedulerPolicy bot = SchedulerPolicyLattice::bottom();
    [[maybe_unused]] SchedulerPolicy top = SchedulerPolicyLattice::top();

    // Mid-tier ops — the load-bearing RoundRobin vs Fifo pair + the
    // Other TSC-read threshold.
    SchedulerPolicy rr   = SchedulerPolicy::RoundRobin;
    SchedulerPolicy fifo = SchedulerPolicy::Fifo;
    SchedulerPolicy other = SchedulerPolicy::Other;
    [[maybe_unused]] SchedulerPolicy j2 = SchedulerPolicyLattice::join(rr, fifo);   // Fifo
    [[maybe_unused]] SchedulerPolicy m2 = SchedulerPolicyLattice::meet(rr, fifo);   // RoundRobin
    [[maybe_unused]] bool tsc_ok        = SchedulerPolicyLattice::leq(other, fifo); // true

    // Graded<Absolute, FifoClass, T> at runtime.
    OneByteValue v{42};
    FifoGraded<OneByteValue> initial{v, scheduler_policy::FifoClass::bottom()};
    auto widened  = initial.weaken(scheduler_policy::FifoClass::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(scheduler_policy::FifoClass::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<SchedulerPolicy>::element_type → SchedulerPolicy at runtime.
    scheduler_policy::FifoClass::element_type e{};
    [[maybe_unused]] SchedulerPolicy rec = e;
}

}  // namespace detail::scheduler_policy_lattice_self_test

}  // namespace crucible::algebra::lattices
