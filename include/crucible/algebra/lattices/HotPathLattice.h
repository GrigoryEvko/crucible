#pragma once

// ── crucible::algebra::lattices::HotPathLattice ─────────────────────
//
// Three-tier total-order chain lattice over the hot-path budget
// spectrum.  The grading axis underlying the HotPath wrapper from
// 28_04_2026_effects.md §4.3.2 — the second Month-2 chain lattice
// from the §13.2 plan, immediately following DetSafe (the 8th-axiom
// enforcer).  Together they thread through the canonical wrapper-
// nesting order (HotPath ⊃ DetSafe ⊃ NumericalTier ⊃ ...) per 28_04
// §4.7.
//
// ── The classification ──────────────────────────────────────────────
//
// Each tier names a CLASS of work-budget envelope.  A function
// declared at tier T promises to perform ONLY operations admissible
// at tier T (or at a STRONGER tier — stronger budgets are admissible
// in weaker contexts).
//
//     Hot     — Foreground hot path.  No allocation, no syscall,
//                no block, no mutex acquisition, no futex, no
//                condition_variable, no I/O.  Per CLAUDE.md §IX:
//                bounded by the per-call shape (atomic ops + cache-
//                line touches; no kernel-mediated transition).  The
//                strongest possible budget; admissible at every
//                consumer site.  Production examples: TraceRing::
//                try_push, MetaLog::push, Vessel::dispatch_op
//                (foreground), shadow-handle dispatch.
//     Warm    — Background-but-bounded.  Allocation OK (arena, pool,
//                jemalloc), no syscall on hot inner loop, blocking
//                on bounded queues OK (CV up to ~ms), I/O permitted
//                only if pre-staged.  Production examples:
//                BackgroundThread::drain, KernelCache compile
//                workers, MerkleDag::build.
//     Cold    — Cold path.  Block / IO / syscall OK without bound.
//                Cipher::flush_cold, fsync, Canopy gossip wire
//                writes, TraceLoader::load_from_disk, Mimic
//                MAP-Elites batch eval.  The weakest budget; admits
//                only Cold-tier consumers.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class HotPathTier ∈ {Cold, Warm, Hot}.
// Order:   Cold ⊑ Warm ⊑ Hot.
//
// Bottom = Cold  (weakest budget; satisfies only Cold-tolerating
//                 consumers).
// Top    = Hot   (strongest budget; satisfies every consumer — a
//                 hot-path-safe function is admissible at warm-path
//                 or cold-path call sites).
// Join   = max   (the more-restricted of two providers — what BOTH
//                 consumers will accept).
// Meet   = min   (the less-restricted of two providers — what EITHER
//                 consumer will accept).
//
// ── Direction convention (matches Tolerance / Consistency / Lifetime
//                          / DetSafe) ──────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-budget consumer is satisfied by a stronger-budget
// provider" — a Hot-tier function can be invoked from any context
// because Hot is the strongest budget possible.
//
// This is the Crucible-standard subsumption-up direction, shared
// with ToleranceLattice (BITEXACT = top), ConsistencyLattice (STRONG
// = top), LifetimeLattice (PER_FLEET = top), DetSafeLattice (Pure =
// top).
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.2 SPEC ──────────────
//
// The spec's enum ordinals (Hot=0, Warm=1, Cold=2) put Hot at the
// BOTTOM of the chain by ordinal.  This implementation INVERTS that
// ordering (Cold=0, Warm=1, Hot=2) to keep the lattice's chain
// direction uniform with the four sister chain lattices listed above
// — which all put the strongest constraint at the TOP.  The
// SEMANTIC contract from the spec ("Hot satisfies any tier") is
// preserved exactly:
//
//   HotPath<Hot>::satisfies<Warm>  = leq(Warm, Hot)  = true ✓
//   HotPath<Hot>::satisfies<Cold>  = leq(Cold, Hot)  = true ✓
//   HotPath<Cold>::satisfies<Hot>  = leq(Hot, Cold)  = false ✓
//   HotPath<Cold>::satisfies<Cold> = leq(Cold, Cold) = true ✓
//
// The only effect of the inversion is that `HotPathTier::Hot ==
// uint8_t{2}` rather than `uint8_t{0}` — purely an enum-encoding
// choice with zero impact on type-level semantics.  Production
// callers that pattern-match on the underlying integer (rare) need
// to be aware; callers that use the enum names (the vast majority)
// see no difference.  Mirrors the DetSafeLattice direction
// inversion (NDS=0, Pure=6 vs spec's Pure=0).
//
//   Axiom coverage:
//     TypeSafe — HotPathTier is a strong scoped enum (`enum class :
//                uint8_t`); cross-tier mixing requires
//                `std::to_underlying` and is surfaced at the call
//                site instead of silently typed.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     three-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tier via
//     `HotPathLattice::At<HotPathTier::Hot>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors ConfLattice::At<Conf>, ToleranceLattice::At<Tolerance>,
// ConsistencyLattice::At<Consistency>, DetSafeLattice::At<DetSafeTier>:
// a per-HotPathTier singleton sub-lattice with empty element_type,
// used when an op's HotPath tier is fixed at the type level (typical
// for hot-path function signatures, scheduler-policy gates, the
// dispatcher's per-shape lowering admission).
// `Graded<Absolute, HotPathLattice::At<HotPathTier::Hot>, T>` pays
// zero runtime overhead for the grade itself.
//
// See FOUND-G18 (this file) for the lattice itself; FOUND-G19
// (safety/HotPath.h) for the type-pinned wrapper; 28_04_2026_effects.md
// §4.3.2 for the production-call-site rationale; CLAUDE.md §IX
// (concurrency / cache-tier discipline) for the hot-path semantics.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── HotPathTier — chain over the work-budget envelope spectrum ──────
//
// Ordinal convention: Cold=0 (bottom) ... Hot=2 (top), matching the
// Tolerance/Consistency/Lifetime/DetSafe project convention
// (bottom=0).  This INVERTS the 28_04 §4.3.2 spec's ordinal hint;
// semantic contract (Hot satisfies any consumer) is preserved.  See
// lattice docblock above for the divergence rationale.
enum class HotPathTier : std::uint8_t {
    Cold = 0,    // bottom: block / IO / syscall OK without bound
    Warm = 1,    // background-but-bounded: alloc OK, no syscall on hot loop
    Hot  = 2,    // top: foreground hot path; no alloc / syscall / block
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t hot_path_tier_count =
    std::meta::enumerators_of(^^HotPathTier).size();

[[nodiscard]] consteval std::string_view hot_path_tier_name(HotPathTier t) noexcept {
    switch (t) {
        case HotPathTier::Cold: return "Cold";
        case HotPathTier::Warm: return "Warm";
        case HotPathTier::Hot:  return "Hot";
        default:                return std::string_view{"<unknown HotPathTier>"};
    }
}

// ── Full HotPathLattice (chain order) ───────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<HotPathTier> — see
// ChainLattice.h for the rationale.
struct HotPathLattice : ChainLatticeOps<HotPathTier> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return HotPathTier::Cold;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return HotPathTier::Hot;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "HotPathLattice";
    }

    // ── At<T>: singleton sub-lattice at a fixed type-level tier ─────
    //
    // Used by per-call-site HotPath-pinned wrappers:
    //   using HotCounter =
    //       Graded<Absolute, HotPathLattice::At<Hot>, ...>;
    template <HotPathTier T>
    struct At {
        struct element_type {
            using hot_path_tier_value_type = HotPathTier;
            [[nodiscard]] constexpr operator hot_path_tier_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr HotPathTier tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case HotPathTier::Cold: return "HotPathLattice::At<Cold>";
                case HotPathTier::Warm: return "HotPathLattice::At<Warm>";
                case HotPathTier::Hot:  return "HotPathLattice::At<Hot>";
                default:                return "HotPathLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace hot_path_tier {
    using ColdTier = HotPathLattice::At<HotPathTier::Cold>;
    using WarmTier = HotPathLattice::At<HotPathTier::Warm>;
    using HotTier  = HotPathLattice::At<HotPathTier::Hot>;
}  // namespace hot_path_tier

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::hot_path_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(hot_path_tier_count == 3,
    "HotPathTier catalog diverged from {Cold, Warm, Hot}; confirm "
    "intent and update the dispatcher's hot-path admission gates + "
    "scheduler-policy plumbing.");

[[nodiscard]] consteval bool every_hot_path_tier_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^HotPathTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (hot_path_tier_name([:en:]) ==
            std::string_view{"<unknown HotPathTier>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_hot_path_tier_has_name(),
    "hot_path_tier_name() switch missing arm for at least one tier — "
    "add the arm or the new tier leaks the '<unknown HotPathTier>' "
    "sentinel into Augur's debug output.");

// Concept conformance — full lattice + each At<T> sub-lattice.
static_assert(Lattice<HotPathLattice>);
static_assert(BoundedLattice<HotPathLattice>);
static_assert(Lattice<hot_path_tier::ColdTier>);
static_assert(Lattice<hot_path_tier::WarmTier>);
static_assert(Lattice<hot_path_tier::HotTier>);
static_assert(BoundedLattice<hot_path_tier::HotTier>);

// Negative concept assertions — pin HotPathLattice's character.
static_assert(!UnboundedLattice<HotPathLattice>);
static_assert(!Semiring<HotPathLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<hot_path_tier::ColdTier::element_type>);
static_assert(std::is_empty_v<hot_path_tier::WarmTier::element_type>);
static_assert(std::is_empty_v<hot_path_tier::HotTier::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (HotPathTier)³ = 27 triples each.  Both verifiers extracted into
// ChainLattice.h — adding a new tier auto-extends coverage.
static_assert(verify_chain_lattice_exhaustive<HotPathLattice>(),
    "HotPathLattice's chain-order lattice axioms must hold at every "
    "(HotPathTier)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<HotPathLattice>(),
    "HotPathLattice's chain order must satisfy distributivity at every "
    "(HotPathTier)³ triple — a chain order always does, so failure "
    "would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing, with Hot
// at the top (strongest budget) and Cold at the bottom.
static_assert( HotPathLattice::leq(HotPathTier::Cold, HotPathTier::Warm));
static_assert( HotPathLattice::leq(HotPathTier::Warm, HotPathTier::Hot));
static_assert( HotPathLattice::leq(HotPathTier::Cold, HotPathTier::Hot));    // transitive endpoints
static_assert(!HotPathLattice::leq(HotPathTier::Hot,  HotPathTier::Cold));
static_assert(!HotPathLattice::leq(HotPathTier::Hot,  HotPathTier::Warm));
static_assert(!HotPathLattice::leq(HotPathTier::Warm, HotPathTier::Cold));

// Pin bottom / top to the chain endpoints.
static_assert(HotPathLattice::bottom() == HotPathTier::Cold);
static_assert(HotPathLattice::top()    == HotPathTier::Hot);

// Join strengthens (max); meet weakens (min).
static_assert(HotPathLattice::join(HotPathTier::Cold, HotPathTier::Hot)
              == HotPathTier::Hot);
static_assert(HotPathLattice::join(HotPathTier::Warm, HotPathTier::Cold)
              == HotPathTier::Warm);
static_assert(HotPathLattice::meet(HotPathTier::Cold, HotPathTier::Hot)
              == HotPathTier::Cold);
static_assert(HotPathLattice::meet(HotPathTier::Warm, HotPathTier::Hot)
              == HotPathTier::Warm);

// Diagnostic names.
static_assert(HotPathLattice::name() == "HotPathLattice");
static_assert(hot_path_tier::ColdTier::name() == "HotPathLattice::At<Cold>");
static_assert(hot_path_tier::WarmTier::name() == "HotPathLattice::At<Warm>");
static_assert(hot_path_tier::HotTier::name()  == "HotPathLattice::At<Hot>");

// Reflection-driven coverage check on At<T>::name().
[[nodiscard]] consteval bool every_at_hot_path_tier_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^HotPathTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (HotPathLattice::At<([:en:])>::name() ==
            std::string_view{"HotPathLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_hot_path_tier_has_name(),
    "HotPathLattice::At<T>::name() switch missing an arm for at "
    "least one tier — add the arm or the new tier leaks the "
    "'HotPathLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(hot_path_tier::ColdTier::tier == HotPathTier::Cold);
static_assert(hot_path_tier::WarmTier::tier == HotPathTier::Warm);
static_assert(hot_path_tier::HotTier::tier  == HotPathTier::Hot);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// HotTier — the most semantically-loaded tier (foreground hot-path
// dispatch + scheduler admission gate).  Witnessed against arithmetic
// T to pin parity across the trivially-default-constructible axis.
template <typename T_>
using HotGraded = Graded<ModalityKind::Absolute, hot_path_tier::HotTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, double);

// WarmTier — bg-thread tier; production: BackgroundThread::drain.
template <typename T_>
using WarmGraded = Graded<ModalityKind::Absolute, hot_path_tier::WarmTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmGraded, EightByteValue);

// ColdTier — block-OK tier; production: Cipher::flush_cold.
template <typename T_>
using ColdGraded = Graded<ModalityKind::Absolute, hot_path_tier::ColdTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.
inline void runtime_smoke_test() {
    // Full HotPathLattice ops at runtime.
    HotPathTier a = HotPathTier::Cold;
    HotPathTier b = HotPathTier::Hot;
    [[maybe_unused]] bool        l1   = HotPathLattice::leq(a, b);
    [[maybe_unused]] HotPathTier j1   = HotPathLattice::join(a, b);
    [[maybe_unused]] HotPathTier m1   = HotPathLattice::meet(a, b);
    [[maybe_unused]] HotPathTier bot  = HotPathLattice::bottom();
    [[maybe_unused]] HotPathTier topv = HotPathLattice::top();

    // Mid-tier ops — chain through the warm boundary.
    HotPathTier warm = HotPathTier::Warm;
    [[maybe_unused]] HotPathTier j2 = HotPathLattice::join(warm, a);    // Warm
    [[maybe_unused]] HotPathTier m2 = HotPathLattice::meet(warm, b);    // Warm

    // Graded<Absolute, HotTier, T> at runtime.
    OneByteValue v{42};
    HotGraded<OneByteValue> initial{v, hot_path_tier::HotTier::bottom()};
    auto widened   = initial.weaken(hot_path_tier::HotTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(hot_path_tier::HotTier::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<HotPathTier>::element_type → HotPathTier at runtime.
    hot_path_tier::HotTier::element_type e{};
    [[maybe_unused]] HotPathTier rec = e;
}

}  // namespace detail::hot_path_lattice_self_test

}  // namespace crucible::algebra::lattices
