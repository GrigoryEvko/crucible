#pragma once

// ── crucible::algebra::lattices::SuspendBehaviorLattice ─────────────
//
// Three-element total-order chain lattice over the clock-PAUSE-on-
// suspend spectrum — the BOOTTIME-vs-MONOTONIC distinction that is
// load-bearing in warden/DeadlineWatchdog.h:228 (Agent 6 §3.1,
// Scenario 6).
//
// ── The classification ──────────────────────────────────────────────
//
//     Unknown          — the clock's suspend behavior is NOT declared.
//                         The weakest claim: a consumer cannot rely on
//                         anything about how this time source treats a
//                         system-suspend window.  Bottom of the chain.
//     PausesOnSuspend   — the clock STOPS advancing while the system is
//                         suspended (CLOCK_MONOTONIC — which
//                         `std::chrono::steady_clock` maps to on Linux).
//                         Correct for within-run interval timing, WRONG
//                         for any elapsed-real-time question that may
//                         straddle a suspend.
//     KeepsTicking      — the clock keeps advancing THROUGH suspend
//                         (CLOCK_BOOTTIME): suspend-inclusive elapsed.
//                         The strongest guarantee — a KeepsTicking
//                         provider also serves any consumer that only
//                         needed within-run monotonicity, so it subsumes
//                         PausesOnSuspend.  Top of the chain.
//
// ── The bug this axis exists to forbid ──────────────────────────────
//
// warden/DeadlineWatchdog.h:228 reads `steady_clock::now()` (→
// CLOCK_MONOTONIC → PausesOnSuspend).  After a 10-minute system
// suspend, `now_ns - window_started_ns_` stays SMALL — the monotonic
// clock paused while ~600s of real wall time elapsed — so the watchdog
// reports Healthy when it should fire FAILURE.  The fix is
// CLOCK_BOOTTIME (KeepsTicking), but the existing wrapper provides no
// way to SAY "I require suspend-inclusive elapsed."  This lattice is
// that vocabulary: a deadline consumer declares a `KeepsTicking`
// requirement, and `leq(requirement, provider)` rejects a
// `PausesOnSuspend` (or `Unknown`) clock at the type level (wired by
// the V-188 SuspendBehavior wrapper + V-194 watchdog migration).
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class SuspendBehavior ∈ {Unknown, PausesOnSuspend,
//          KeepsTicking}.
// Order:   Unknown ⊑ PausesOnSuspend ⊑ KeepsTicking.
//
// Bottom = Unknown        (weakest; satisfies only Unknown-tolerating
//                          consumers).
// Top    = KeepsTicking   (strongest; suspend-inclusive — satisfies
//                          every consumer).
// Join   = max            (the more-suspend-resilient of two providers —
//                          what BOTH consumers will accept).
// Meet   = min            (the less-suspend-resilient — what EITHER
//                          consumer will accept).
//
// ── Direction convention (matches DetSafe / Tolerance / Consistency) ─
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-suspend requirement is satisfied by a stronger-
// suspend provider" — KeepsTicking subsumes PausesOnSuspend at any
// consumer point because keeping-ticking-through-suspend is the
// strictly stronger promise.  This is the Crucible-standard
// subsumption-up direction shared with DetSafeLattice (Pure = top),
// ToleranceLattice (BITEXACT = top), ConsistencyLattice (STRONG = top).
//
// ── Composition (V-184) ─────────────────────────────────────────────
//
// SuspendBehaviorLattice is one of THREE axes ProductLattice (V-184
// ClockSourceLattice) composes — DetSafe × SuspendBehavior × Pinning.
// A `ClockSource` annotation pins all three independently; the suspend
// axis answers "does it survive suspend", orthogonal to "is it replay-
// deterministic" (DetSafe) and "does reading it need CPU affinity"
// (Pinning).
//
// ── This header pulls NO row_hash machinery ─────────────────────────
//
// The row_hash key is the WRAPPER, never the lattice At<> — exactly as
// BarrierStrengthLattice.h:130 / HwInstructionLattice.h:136 /
// MemoryScopeLattice.h document.  FIXY-V-188 ships safety/
// SuspendBehavior.h (the Graded<Absolute, SuspendBehaviorLattice::At<B>,
// T> carrier) PLUS the `row_hash_contribution<SuspendBehavior<...>>`
// federation-cache discriminator in safety/diag/RowHashFold.h, mirroring
// how MemoryScopeLattice (V-265) defers its wrapper + row_hash to
// safety/ScopedFence.h (V-267).  `algebra/lattices/` sits BELOW
// `safety/diag/`; keying row_hash on the lattice At<> (a nested template,
// not forward-declarable) would invert that layering.  The lattice layer
// stays pure order theory.
//
//   Axiom coverage:
//     TypeSafe — SuspendBehavior is a strong scoped enum (`enum class :
//                uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare + select; the three-
//     element domain compiles to a 1-byte field with one branch.  When
//     wrapped at a fixed type-level behavior via
//     `SuspendBehaviorLattice::At<KeepsTicking>`, the grade EBO-collapses
//     to zero bytes.
//
// See Agent 6 §3.1 (clock-source axis decomposition) for the design
// rationale; FIXY-V-184 (ClockSourceLattice) for the composite; FIXY-
// V-188 (safety/SuspendBehavior.h) for the type-pinned wrapper + row_hash;
// CRUCIBLE.md §II.8 (DetSafe) for the sibling determinism axis.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── SuspendBehavior — chain over the clock-pause-on-suspend spectrum ─
//
// Ordinal convention: Unknown=0 (bottom) ... KeepsTicking=2 (top),
// matching the DetSafe/Tolerance/Consistency project convention
// (bottom=0, strongest constraint at top).
enum class SuspendBehavior : std::uint8_t {
    Unknown         = 0,    // bottom: suspend behavior undeclared
    PausesOnSuspend = 1,    // CLOCK_MONOTONIC — stops while suspended
    KeepsTicking    = 2,    // top: CLOCK_BOOTTIME — suspend-inclusive
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t suspend_behavior_count =
    std::meta::enumerators_of(^^SuspendBehavior).size();

[[nodiscard]] consteval std::string_view
suspend_behavior_name(SuspendBehavior b) noexcept {
    switch (b) {
        case SuspendBehavior::Unknown:         return "Unknown";
        case SuspendBehavior::PausesOnSuspend: return "PausesOnSuspend";
        case SuspendBehavior::KeepsTicking:    return "KeepsTicking";
        default: return std::string_view{"<unknown SuspendBehavior>"};
    }
}

// ── Full SuspendBehaviorLattice (chain order) ───────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<SuspendBehavior> — see
// ChainLattice.h for the rationale.
struct SuspendBehaviorLattice : ChainLatticeOps<SuspendBehavior> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return SuspendBehavior::Unknown;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return SuspendBehavior::KeepsTicking;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "SuspendBehaviorLattice";
    }

    // ── At<B>: singleton sub-lattice at a fixed type-level behavior ──
    //
    // Used by the per-call-site SuspendBehavior-pinned wrapper (V-188):
    //   using BootClock =
    //       Graded<Absolute, SuspendBehaviorLattice::At<KeepsTicking>, ...>;
    template <SuspendBehavior B>
    struct At {
        struct element_type {
            using suspend_behavior_value_type = SuspendBehavior;
            [[nodiscard]] constexpr operator suspend_behavior_value_type() const noexcept {
                return B;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr SuspendBehavior behavior = B;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (B) {
                case SuspendBehavior::Unknown:
                    return "SuspendBehaviorLattice::At<Unknown>";
                case SuspendBehavior::PausesOnSuspend:
                    return "SuspendBehaviorLattice::At<PausesOnSuspend>";
                case SuspendBehavior::KeepsTicking:
                    return "SuspendBehaviorLattice::At<KeepsTicking>";
                default:
                    return "SuspendBehaviorLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace suspend_behavior {
    using UnknownBehavior      = SuspendBehaviorLattice::At<SuspendBehavior::Unknown>;
    using PausesOnSuspendClock = SuspendBehaviorLattice::At<SuspendBehavior::PausesOnSuspend>;
    using KeepsTickingClock    = SuspendBehaviorLattice::At<SuspendBehavior::KeepsTicking>;
}  // namespace suspend_behavior

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::suspend_behavior_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(suspend_behavior_count == 3,
    "SuspendBehavior catalog diverged from {Unknown, PausesOnSuspend, "
    "KeepsTicking}; adding a behavior requires extending both name "
    "switches AND bumping the V-184 ClockSourceLattice composition + "
    "V-188 SuspendBehavior wrapper satisfies<> gate.");

[[nodiscard]] consteval bool every_suspend_behavior_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SuspendBehavior));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (suspend_behavior_name([:en:]) ==
            std::string_view{"<unknown SuspendBehavior>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_suspend_behavior_has_name(),
    "suspend_behavior_name() switch missing arm for at least one "
    "behavior — add the arm or the new behavior leaks the "
    "'<unknown SuspendBehavior>' sentinel into observer debug output.");

// Concept conformance — full lattice + each At<B> sub-lattice.
static_assert(Lattice<SuspendBehaviorLattice>);
static_assert(BoundedLattice<SuspendBehaviorLattice>);
static_assert(Lattice<suspend_behavior::UnknownBehavior>);
static_assert(Lattice<suspend_behavior::KeepsTickingClock>);
static_assert(BoundedLattice<suspend_behavior::KeepsTickingClock>);

// Negative concept assertions — pin SuspendBehaviorLattice's character.
static_assert(!UnboundedLattice<SuspendBehaviorLattice>);
static_assert(!Semiring<SuspendBehaviorLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<suspend_behavior::UnknownBehavior::element_type>);
static_assert(std::is_empty_v<suspend_behavior::PausesOnSuspendClock::element_type>);
static_assert(std::is_empty_v<suspend_behavior::KeepsTickingClock::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (SuspendBehavior)³ = 27 triples each.  Both verifiers extracted into
// ChainLattice.h — adding a new behavior auto-extends coverage.
static_assert(verify_chain_lattice_exhaustive<SuspendBehaviorLattice>(),
    "SuspendBehaviorLattice's chain-order lattice axioms must hold at "
    "every (SuspendBehavior)³ triple — failure indicates a defect in "
    "leq/join/meet or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<SuspendBehaviorLattice>(),
    "SuspendBehaviorLattice's chain order must satisfy distributivity at "
    "every (SuspendBehavior)³ triple — a chain order always does, so "
    "failure would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing, with
// KeepsTicking at the top (strongest suspend resilience).
static_assert( SuspendBehaviorLattice::leq(SuspendBehavior::Unknown,         SuspendBehavior::PausesOnSuspend));
static_assert( SuspendBehaviorLattice::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking));
static_assert( SuspendBehaviorLattice::leq(SuspendBehavior::Unknown,         SuspendBehavior::KeepsTicking));  // transitive endpoints
// The load-bearing subsumption: KeepsTicking satisfies a PausesOnSuspend
// requirement; PausesOnSuspend does NOT satisfy a KeepsTicking one.
static_assert( SuspendBehaviorLattice::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking),
    "SuspendBehaviorLattice: KeepsTicking ⊒ PausesOnSuspend — a boot-clock "
    "provider serves a monotonic-clock requirement.");
static_assert(!SuspendBehaviorLattice::leq(SuspendBehavior::KeepsTicking,    SuspendBehavior::PausesOnSuspend),
    "SuspendBehaviorLattice: PausesOnSuspend ⋣ KeepsTicking — a monotonic "
    "clock does NOT satisfy a suspend-inclusive requirement; this is the "
    "DeadlineWatchdog false-Healthy bug forbidden at the type level.");
static_assert(!SuspendBehaviorLattice::leq(SuspendBehavior::KeepsTicking,    SuspendBehavior::Unknown));

// Pin bottom / top to the chain endpoints.
static_assert(SuspendBehaviorLattice::bottom() == SuspendBehavior::Unknown);
static_assert(SuspendBehaviorLattice::top()    == SuspendBehavior::KeepsTicking);

// Join strengthens (max); meet weakens (min).
static_assert(SuspendBehaviorLattice::join(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::KeepsTicking);
static_assert(SuspendBehaviorLattice::join(SuspendBehavior::PausesOnSuspend, SuspendBehavior::Unknown)
              == SuspendBehavior::PausesOnSuspend);
static_assert(SuspendBehaviorLattice::meet(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::Unknown);
static_assert(SuspendBehaviorLattice::meet(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::PausesOnSuspend);

// Diagnostic names.
static_assert(SuspendBehaviorLattice::name() == "SuspendBehaviorLattice");
static_assert(suspend_behavior::UnknownBehavior::name()      == "SuspendBehaviorLattice::At<Unknown>");
static_assert(suspend_behavior::PausesOnSuspendClock::name() == "SuspendBehaviorLattice::At<PausesOnSuspend>");
static_assert(suspend_behavior::KeepsTickingClock::name()    == "SuspendBehaviorLattice::At<KeepsTicking>");

// Reflection-driven coverage check on At<B>::name().
[[nodiscard]] consteval bool every_at_suspend_behavior_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SuspendBehavior));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (SuspendBehaviorLattice::At<([:en:])>::name() ==
            std::string_view{"SuspendBehaviorLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_suspend_behavior_has_name(),
    "SuspendBehaviorLattice::At<B>::name() switch missing an arm for at "
    "least one behavior — add the arm or the new behavior leaks the "
    "'SuspendBehaviorLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(suspend_behavior::UnknownBehavior::behavior      == SuspendBehavior::Unknown);
static_assert(suspend_behavior::PausesOnSuspendClock::behavior == SuspendBehavior::PausesOnSuspend);
static_assert(suspend_behavior::KeepsTickingClock::behavior    == SuspendBehavior::KeepsTicking);

// ── Layout invariants on Graded<...,At<B>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// KeepsTickingClock — the most semantically-loaded behavior (the boot-
// clock gate the DeadlineWatchdog migration needs).  Witnessed against
// arithmetic T to pin parity across the trivially-default-constructible
// axis.
template <typename T_>
using BootClockGraded = Graded<ModalityKind::Absolute, suspend_behavior::KeepsTickingClock, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockGraded, int);

// PausesOnSuspendClock — the steady_clock default.
template <typename T_>
using MonoClockGraded = Graded<ModalityKind::Absolute, suspend_behavior::PausesOnSuspendClock, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MonoClockGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.
inline void runtime_smoke_test() {
    // Full SuspendBehaviorLattice ops at runtime.
    SuspendBehavior a = SuspendBehavior::Unknown;
    SuspendBehavior b = SuspendBehavior::KeepsTicking;
    [[maybe_unused]] bool            l1  = SuspendBehaviorLattice::leq(a, b);
    [[maybe_unused]] SuspendBehavior j1  = SuspendBehaviorLattice::join(a, b);
    [[maybe_unused]] SuspendBehavior m1  = SuspendBehaviorLattice::meet(a, b);
    [[maybe_unused]] SuspendBehavior bot = SuspendBehaviorLattice::bottom();
    [[maybe_unused]] SuspendBehavior top = SuspendBehaviorLattice::top();

    // Mid-tier ops — the load-bearing PausesOnSuspend vs KeepsTicking pair.
    SuspendBehavior mono = SuspendBehavior::PausesOnSuspend;
    SuspendBehavior boot = SuspendBehavior::KeepsTicking;
    [[maybe_unused]] SuspendBehavior j2 = SuspendBehaviorLattice::join(mono, boot);  // KeepsTicking
    [[maybe_unused]] SuspendBehavior m2 = SuspendBehaviorLattice::meet(mono, boot);  // PausesOnSuspend

    // Graded<Absolute, KeepsTickingClock, T> at runtime.
    OneByteValue v{42};
    BootClockGraded<OneByteValue> initial{v, suspend_behavior::KeepsTickingClock::bottom()};
    auto widened  = initial.weaken(suspend_behavior::KeepsTickingClock::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(suspend_behavior::KeepsTickingClock::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<SuspendBehavior>::element_type → SuspendBehavior at runtime.
    suspend_behavior::KeepsTickingClock::element_type e{};
    [[maybe_unused]] SuspendBehavior rec = e;
}

}  // namespace detail::suspend_behavior_lattice_self_test

}  // namespace crucible::algebra::lattices
