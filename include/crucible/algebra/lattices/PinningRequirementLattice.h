#pragma once

// ── crucible::algebra::lattices::PinningRequirementLattice ──────────
//
// Four-element total-order chain lattice over the CPU-coherence-domain
// spectrum — the rdtsc-needs-pinning axis that is load-bearing in
// bench/bench_harness.h:127 (Agent 6 §3.1, Scenario 2).
//
// ── The classification ──────────────────────────────────────────────
//
// Each element names the BREADTH of the CPU set across which a
// timestamp read stays coherent — equivalently, how far a thread may
// migrate between two reads without corrupting the delta.
//
//     NotRequired       — NO cross-CPU coherence discipline is declared.
//                         The conservative un-annotated default: the
//                         source makes no claim, so a caller can rely on
//                         nothing.  Bottom of the chain.  (Distinct from
//                         CrossSocketSafe: that is a VERIFIED no-pinning-
//                         needed guarantee; this is the ABSENCE of any
//                         claim — exactly as V-181's Unknown ⊥ differs
//                         from its KeepsTicking ⊤.)
//     PerCore           — coherent only within a single core; the thread
//                         MUST be pinned to one core (singleton CPU
//                         mask).  TSC differs per core — e.g. AMD
//                         Bergamo's two CCDs carry a ~50-80 cycle TSC
//                         offset, so a read straddling CCDs reports a
//                         negative delta (wraps to UINT64_MAX) or a
//                         spurious 50ns spike.
//     PerSocket         — coherent within one socket; the thread may
//                         migrate freely among that socket's cores.
//                         E.g. Skylake-X with HT off has invariant TSC
//                         across cores within a socket but offset across
//                         sockets.
//     CrossSocketSafe   — NUMA-coherent invariant TSC across ALL sockets
//                         (recent Intel).  Safe to read under any
//                         placement — the strongest guarantee, subsumes
//                         every narrower coherence requirement.  Top.
//
// ── The bug this axis exists to forbid ──────────────────────────────
//
// bench/bench_harness.h:127 calls `__rdtsc()` inside `rdtsc_start()`.
// `Run::pin_()` (line 1404) attempts `sched_setaffinity` before the
// timed region, BUT (per the line 1431-1438 comment) sched_setaffinity
// does not synchronously migrate — `sched_getcpu()` may still report the
// previous CPU.  Under `Pin::None` (line 1406, only sched_getcpu, no
// real pin), every rdtsc_start/end pair can straddle a migration.  On
// AMD Bergamo that crosses a CCD TSC-offset boundary and the measured
// delta goes backwards.  This lattice is the vocabulary for "this rdtsc
// read REQUIRES at least PerCore coherence"; the V-187 CpuPinned wrapper
// + V-196 bench migration thread that requirement through `leq` so an
// under-pinned read is rejected at the type level.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class PinningRequirement ∈ {NotRequired, PerCore,
//          PerSocket, CrossSocketSafe}.
// Order:   NotRequired ⊑ PerCore ⊑ PerSocket ⊑ CrossSocketSafe.
//
// Bottom = NotRequired      (weakest; no coherence discipline declared).
// Top    = CrossSocketSafe  (strongest; coherent across all sockets —
//                            satisfies every consumer).
// Join   = max              (the wider safe-migration domain two
//                            providers jointly guarantee).
// Meet   = min              (the narrower — what EITHER consumer accepts).
//
// ── Direction convention (matches DetSafe / SuspendBehavior / Tolerance)
//
// Stronger guarantee = HIGHER in the lattice.  `leq(narrow, broad)`
// reads "a narrower-coherence consumer is satisfied by a broader-
// coherence provider" — a PerSocket-coherent source serves a PerCore
// consumer (socket coherence contains core coherence), and a
// CrossSocketSafe source serves everyone.  This is the Crucible-standard
// subsumption-up direction shared with DetSafeLattice (Pure = top),
// SuspendBehaviorLattice (KeepsTicking = top), ToleranceLattice
// (BITEXACT = top).
//
// ── Composition (V-184) ─────────────────────────────────────────────
//
// PinningRequirementLattice is one of THREE axes ProductLattice (V-184
// ClockSourceLattice) composes — DetSafe × SuspendBehavior × Pinning.
// The pinning axis answers "does reading it need CPU affinity, and how
// tight", orthogonal to "is it replay-deterministic" (DetSafe) and "does
// it survive suspend" (SuspendBehavior, V-181).
//
// ── This header pulls NO row_hash machinery ─────────────────────────
//
// The row_hash key is the WRAPPER, never the lattice At<> — exactly as
// BarrierStrengthLattice.h:130 / HwInstructionLattice.h:136 /
// MemoryScopeLattice.h / SuspendBehaviorLattice.h document.  FIXY-V-187
// ships safety/CpuPinned.h (the Graded carrier composing this with the
// existing AffinityLattice) PLUS the row_hash_contribution<CpuPinned<...>>
// federation-cache discriminator in safety/diag/RowHashFold.h, mirroring
// how MemoryScopeLattice (V-265) defers its wrapper + row_hash to
// safety/ScopedFence.h (V-267).  `algebra/lattices/` sits BELOW
// `safety/diag/`; keying row_hash on the lattice At<> (a nested template,
// not forward-declarable) would invert that layering.  The lattice layer
// stays pure order theory.
//
//   Axiom coverage:
//     TypeSafe — PinningRequirement is a strong scoped enum (`enum class
//                : uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare + select; the four-
//     element domain compiles to a 1-byte field with one branch.  When
//     wrapped at a fixed type-level requirement via
//     `PinningRequirementLattice::At<PerCore>`, the grade EBO-collapses
//     to zero bytes.
//
// See Agent 6 §3.1 (clock-source axis decomposition) for the design
// rationale; FIXY-V-184 (ClockSourceLattice) for the composite; FIXY-
// V-187 (safety/CpuPinned.h) for the type-pinned wrapper + row_hash;
// FIXY-V-196 (bench rdtsc CpuPinned proof) for the consuming call site.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── PinningRequirement — chain over the CPU-coherence-domain spectrum ─
//
// Ordinal convention: NotRequired=0 (bottom) ... CrossSocketSafe=3
// (top), matching the DetSafe/SuspendBehavior/Tolerance project
// convention (bottom=0, strongest constraint at top).
enum class PinningRequirement : std::uint8_t {
    NotRequired     = 0,    // bottom: no coherence discipline declared
    PerCore         = 1,    // coherent within one core (singleton mask)
    PerSocket       = 2,    // coherent within one socket
    CrossSocketSafe = 3,    // top: NUMA-coherent TSC across all sockets
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t pinning_requirement_count =
    std::meta::enumerators_of(^^PinningRequirement).size();

[[nodiscard]] consteval std::string_view
pinning_requirement_name(PinningRequirement p) noexcept {
    switch (p) {
        case PinningRequirement::NotRequired:     return "NotRequired";
        case PinningRequirement::PerCore:         return "PerCore";
        case PinningRequirement::PerSocket:       return "PerSocket";
        case PinningRequirement::CrossSocketSafe: return "CrossSocketSafe";
        default: return std::string_view{"<unknown PinningRequirement>"};
    }
}

// ── Full PinningRequirementLattice (chain order) ────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<PinningRequirement> — see
// ChainLattice.h for the rationale.
struct PinningRequirementLattice : ChainLatticeOps<PinningRequirement> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return PinningRequirement::NotRequired;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return PinningRequirement::CrossSocketSafe;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "PinningRequirementLattice";
    }

    // ── At<P>: singleton sub-lattice at a fixed type-level requirement ─
    //
    // Used by the V-187 CpuPinned wrapper:
    //   using CorePinned =
    //       Graded<Absolute, PinningRequirementLattice::At<PerCore>, ...>;
    template <PinningRequirement P>
    struct At {
        struct element_type {
            using pinning_requirement_value_type = PinningRequirement;
            [[nodiscard]] constexpr operator pinning_requirement_value_type() const noexcept {
                return P;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr PinningRequirement requirement = P;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (P) {
                case PinningRequirement::NotRequired:
                    return "PinningRequirementLattice::At<NotRequired>";
                case PinningRequirement::PerCore:
                    return "PinningRequirementLattice::At<PerCore>";
                case PinningRequirement::PerSocket:
                    return "PinningRequirementLattice::At<PerSocket>";
                case PinningRequirement::CrossSocketSafe:
                    return "PinningRequirementLattice::At<CrossSocketSafe>";
                default:
                    return "PinningRequirementLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace pinning_requirement {
    using NotRequiredPin     = PinningRequirementLattice::At<PinningRequirement::NotRequired>;
    using PerCorePin         = PinningRequirementLattice::At<PinningRequirement::PerCore>;
    using PerSocketPin       = PinningRequirementLattice::At<PinningRequirement::PerSocket>;
    using CrossSocketSafePin = PinningRequirementLattice::At<PinningRequirement::CrossSocketSafe>;
}  // namespace pinning_requirement

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::pinning_requirement_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(pinning_requirement_count == 4,
    "PinningRequirement catalog diverged from {NotRequired, PerCore, "
    "PerSocket, CrossSocketSafe}; adding a level requires extending both "
    "name switches AND bumping the V-184 ClockSourceLattice composition "
    "+ V-187 CpuPinned wrapper satisfies<> gate.");

[[nodiscard]] consteval bool every_pinning_requirement_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PinningRequirement));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (pinning_requirement_name([:en:]) ==
            std::string_view{"<unknown PinningRequirement>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_pinning_requirement_has_name(),
    "pinning_requirement_name() switch missing arm for at least one "
    "level — add the arm or the new level leaks the "
    "'<unknown PinningRequirement>' sentinel into observer debug output.");

// Concept conformance — full lattice + each At<P> sub-lattice.
static_assert(Lattice<PinningRequirementLattice>);
static_assert(BoundedLattice<PinningRequirementLattice>);
static_assert(Lattice<pinning_requirement::NotRequiredPin>);
static_assert(Lattice<pinning_requirement::CrossSocketSafePin>);
static_assert(BoundedLattice<pinning_requirement::CrossSocketSafePin>);

// Negative concept assertions — pin PinningRequirementLattice's character.
static_assert(!UnboundedLattice<PinningRequirementLattice>);
static_assert(!Semiring<PinningRequirementLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<pinning_requirement::NotRequiredPin::element_type>);
static_assert(std::is_empty_v<pinning_requirement::PerCorePin::element_type>);
static_assert(std::is_empty_v<pinning_requirement::PerSocketPin::element_type>);
static_assert(std::is_empty_v<pinning_requirement::CrossSocketSafePin::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (PinningRequirement)³ = 64 triples each.  Both verifiers extracted into
// ChainLattice.h — adding a new level auto-extends coverage.
static_assert(verify_chain_lattice_exhaustive<PinningRequirementLattice>(),
    "PinningRequirementLattice's chain-order lattice axioms must hold at "
    "every (PinningRequirement)³ triple — failure indicates a defect in "
    "leq/join/meet or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<PinningRequirementLattice>(),
    "PinningRequirementLattice's chain order must satisfy distributivity "
    "at every (PinningRequirement)³ triple — a chain order always does, so "
    "failure would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing, with
// CrossSocketSafe at the top (widest coherence domain).
static_assert( PinningRequirementLattice::leq(PinningRequirement::NotRequired, PinningRequirement::PerCore));
static_assert( PinningRequirementLattice::leq(PinningRequirement::PerCore,     PinningRequirement::PerSocket));
static_assert( PinningRequirementLattice::leq(PinningRequirement::PerSocket,   PinningRequirement::CrossSocketSafe));
static_assert( PinningRequirementLattice::leq(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe));  // transitive endpoints
// The load-bearing subsumption: a broader-coherence source serves a
// narrower-coherence consumer; the descending direction is FALSE.
static_assert( PinningRequirementLattice::leq(PinningRequirement::PerCore, PinningRequirement::PerSocket),
    "PinningRequirementLattice: PerSocket ⊒ PerCore — a socket-coherent "
    "source serves a per-core consumer (socket coherence contains core).");
static_assert(!PinningRequirementLattice::leq(PinningRequirement::PerSocket, PinningRequirement::PerCore),
    "PinningRequirementLattice: PerCore ⋣ PerSocket — a merely per-core-"
    "coherent source does NOT serve a consumer migrating across the socket; "
    "this is the rdtsc cross-CCD negative-delta bug forbidden at the type "
    "level.");
static_assert(!PinningRequirementLattice::leq(PinningRequirement::CrossSocketSafe, PinningRequirement::NotRequired));

// Pin bottom / top to the chain endpoints.
static_assert(PinningRequirementLattice::bottom() == PinningRequirement::NotRequired);
static_assert(PinningRequirementLattice::top()    == PinningRequirement::CrossSocketSafe);

// Join widens (max); meet narrows (min).
static_assert(PinningRequirementLattice::join(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe)
              == PinningRequirement::CrossSocketSafe);
static_assert(PinningRequirementLattice::join(PinningRequirement::PerCore, PinningRequirement::PerSocket)
              == PinningRequirement::PerSocket);
static_assert(PinningRequirementLattice::meet(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe)
              == PinningRequirement::NotRequired);
static_assert(PinningRequirementLattice::meet(PinningRequirement::PerCore, PinningRequirement::PerSocket)
              == PinningRequirement::PerCore);

// Diagnostic names.
static_assert(PinningRequirementLattice::name() == "PinningRequirementLattice");
static_assert(pinning_requirement::NotRequiredPin::name()     == "PinningRequirementLattice::At<NotRequired>");
static_assert(pinning_requirement::PerCorePin::name()         == "PinningRequirementLattice::At<PerCore>");
static_assert(pinning_requirement::PerSocketPin::name()       == "PinningRequirementLattice::At<PerSocket>");
static_assert(pinning_requirement::CrossSocketSafePin::name() == "PinningRequirementLattice::At<CrossSocketSafe>");

// Reflection-driven coverage check on At<P>::name().
[[nodiscard]] consteval bool every_at_pinning_requirement_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PinningRequirement));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (PinningRequirementLattice::At<([:en:])>::name() ==
            std::string_view{"PinningRequirementLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_pinning_requirement_has_name(),
    "PinningRequirementLattice::At<P>::name() switch missing an arm for at "
    "least one level — add the arm or the new level leaks the "
    "'PinningRequirementLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(pinning_requirement::NotRequiredPin::requirement     == PinningRequirement::NotRequired);
static_assert(pinning_requirement::PerCorePin::requirement         == PinningRequirement::PerCore);
static_assert(pinning_requirement::PerSocketPin::requirement       == PinningRequirement::PerSocket);
static_assert(pinning_requirement::CrossSocketSafePin::requirement == PinningRequirement::CrossSocketSafe);

// ── Layout invariants on Graded<...,At<P>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// PerCorePin — the most semantically-loaded level (the singleton-mask
// gate the bench rdtsc migration needs).  Witnessed against arithmetic T
// to pin parity across the trivially-default-constructible axis.
template <typename T_>
using CorePinnedGraded = Graded<ModalityKind::Absolute, pinning_requirement::PerCorePin, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, int);

// CrossSocketSafePin — the no-pin-needed top.
template <typename T_>
using CrossSocketGraded = Graded<ModalityKind::Absolute, pinning_requirement::CrossSocketSafePin, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CrossSocketGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.
inline void runtime_smoke_test() {
    // Full PinningRequirementLattice ops at runtime.
    PinningRequirement a = PinningRequirement::NotRequired;
    PinningRequirement b = PinningRequirement::CrossSocketSafe;
    [[maybe_unused]] bool               l1  = PinningRequirementLattice::leq(a, b);
    [[maybe_unused]] PinningRequirement j1  = PinningRequirementLattice::join(a, b);
    [[maybe_unused]] PinningRequirement m1  = PinningRequirementLattice::meet(a, b);
    [[maybe_unused]] PinningRequirement bot = PinningRequirementLattice::bottom();
    [[maybe_unused]] PinningRequirement top = PinningRequirementLattice::top();

    // Mid-tier ops — the load-bearing PerCore vs PerSocket pair.
    PinningRequirement core   = PinningRequirement::PerCore;
    PinningRequirement socket = PinningRequirement::PerSocket;
    [[maybe_unused]] PinningRequirement j2 = PinningRequirementLattice::join(core, socket);  // PerSocket
    [[maybe_unused]] PinningRequirement m2 = PinningRequirementLattice::meet(core, socket);  // PerCore

    // Graded<Absolute, PerCorePin, T> at runtime.
    OneByteValue v{42};
    CorePinnedGraded<OneByteValue> initial{v, pinning_requirement::PerCorePin::bottom()};
    auto widened  = initial.weaken(pinning_requirement::PerCorePin::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(pinning_requirement::PerCorePin::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<PinningRequirement>::element_type → PinningRequirement at runtime.
    pinning_requirement::PerCorePin::element_type e{};
    [[maybe_unused]] PinningRequirement rec = e;
}

}  // namespace detail::pinning_requirement_lattice_self_test

}  // namespace crucible::algebra::lattices
