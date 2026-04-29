#pragma once

// ── crucible::algebra::lattices::ResidencyHeatLattice ───────────────
//
// Three-tier total-order chain lattice over the storage-heat
// (cache-tier) spectrum.  The grading axis underlying the
// ResidencyHeat wrapper from 28_04_2026_effects.md §4.3.8.
//
// THE LOAD-BEARING USE CASE: KernelCache L1/L2/L3 + Augur metric
// heat-tier classification.  Distinct from CipherTier's storage-
// PERSISTENCE axis: ResidencyHeat captures HOW HOT the access
// pattern is (working-set residency / cache-tier), CipherTier
// captures WHERE the storage LIVES (durability tier — RAM vs
// NVMe vs S3).  Both are 3-tier chains with Hot at the top, but
// the semantic axes are orthogonal.
//
// Composes orthogonally with the seven sister chain wrappers
// (DetSafe / HotPath / Wait / MemOrder / Progress / AllocClass /
// CipherTier) via wrapper-nesting per 28_04 §4.7.  A
// `KernelCache::publish` may produce a CipherTier<Warm>
// (NVMe-resident shard) at ResidencyHeat<Hot> (kept warm in the
// L1 IR002 working set) — the two tiers never collapse.
//
// ── The classification ──────────────────────────────────────────────
//
//     Hot   — Working-set hot.  Lives in L1 (KernelCache IR002 most-
//              accessed slots).  Augur metric: P95 access in last
//              window.  ~ns access latency.  The strongest
//              residency-heat claim — closest to the consumer.
//              Production: hottest 32 KB of KernelCache L1 IR002,
//              hot-watch Augur per-axis drift counters, hottest
//              ~ms of TraceRing tail.
//     Warm  — Working-set warm.  Lives in L2 (KernelCache IR003* per-
//              vendor-family slabs).  Augur metric: accessed within
//              last few iterations.  ~tens of ns access latency.
//              Production: KernelCache IR003* warm slabs, Augur
//              broader-window aggregates, MetaLog drain region.
//     Cold  — Working-set cold.  Lives in L3 (KernelCache compiled
//              bytecode large slabs) or DRAM.  Augur metric:
//              eviction-candidate.  ~hundreds of ns access latency.
//              The weakest residency-heat claim — farthest from the
//              consumer.  Production: KernelCache L3 compiled bytes
//              archive, Augur cold metric long-window aggregates.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class ResidencyHeatTag ∈ {Cold, Warm, Hot}.
// Order:   Cold ⊑ Warm ⊑ Hot.
//
// Bottom = Cold (slowest, most permissive admission — anything
//                in the program can request a Cold-tier slot).
// Top    = Hot  (fastest, most restrictive — only the hottest
//                working-set members earn an L1 slot).
// Join   = max  (the more-restricted of two providers).
// Meet   = min  (the more-permissive of two providers).
//
// ── Direction convention ────────────────────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-heat consumer is satisfied by a stronger-heat
// provider".  A Hot-residency value is admissible everywhere
// because it's the most-cached claim possible (Hot trivially
// satisfies a Warm-OK or Cold-OK consumer).
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.8 SPEC ──────────────
//
// Spec puts Hot=0 ... Cold=2 (working-set ordinal). This
// implementation INVERTS to Cold=0 ... Hot=2 to keep the chain
// direction uniform with the seven sister chain lattices.  The
// SEMANTIC contract from the spec ("KernelCache L1 hottest, L3
// coldest; Augur per-axis heat tracking") is preserved exactly:
//
//   ResidencyHeat<Hot>::satisfies<Warm>  = leq(Warm, Hot)  = true ✓
//   ResidencyHeat<Cold>::satisfies<Hot>  = leq(Hot, Cold)  = false ✓
//   ResidencyHeat<Hot>::satisfies<Cold>  = leq(Cold, Hot)  = true ✓
//
// Same shape as HotPathLattice + CipherTierLattice (all three
// 3-tier chains, all three Hot-at-top, all three inverted from
// spec); the three lattices are SEMANTICALLY distinct
// (ResidencyHeat = cache-heat, CipherTier = storage residency,
// HotPath = execution budget) but structurally identical.
//
//   Axiom coverage:
//     TypeSafe — ResidencyHeatTag is a strong scoped enum;
//                cross-tier mixing requires
//                `std::to_underlying`.
//   Runtime cost:
//     leq / join / meet — single integer compare; the three-element
//     domain compiles to a 1-byte field.  When wrapped at a fixed
//     type-level tier via `ResidencyHeatLattice::At<...::Hot>`,
//     the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the seven sister chain lattices: a per-ResidencyHeatTag
// singleton sub-lattice with empty element_type.
//
// See FOUND-G48 (this file) for the lattice; FOUND-G49
// (safety/ResidencyHeat.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.8 for the production-call-site
// rationale; CRUCIBLE.md §L2 (KernelCache three-level cache) +
// §L15 (Augur metric heat) for the load-bearing consumers.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── ResidencyHeatTag — chain over working-set residency heat ──────
//
// Ordinal convention: Cold=0 (bottom) ... Hot=2 (top), per project
// convention (bottom=0=weakest).  INVERTS the spec's working-set
// ordinal hint; semantic contract preserved.  Same shape as
// HotPathTier + CipherTierTag but the type is DISTINCT (different
// scoped enum class) — see lattice docblock for the orthogonal-
// axis rationale.
enum class ResidencyHeatTag : std::uint8_t {
    Cold = 0,    // bottom: L3 / DRAM working-set tail (~hundreds ns)
    Warm = 1,    // L2 working-set body (~tens of ns)
    Hot  = 2,    // top: L1 hottest working-set (~ns)
};

inline constexpr std::size_t residency_heat_tag_count =
    std::meta::enumerators_of(^^ResidencyHeatTag).size();

[[nodiscard]] consteval std::string_view residency_heat_tag_name(
    ResidencyHeatTag t) noexcept {
    switch (t) {
        case ResidencyHeatTag::Cold: return "Cold";
        case ResidencyHeatTag::Warm: return "Warm";
        case ResidencyHeatTag::Hot:  return "Hot";
        default:                     return std::string_view{
            "<unknown ResidencyHeatTag>"};
    }
}

// ── Full ResidencyHeatLattice (chain order) ─────────────────────────
struct ResidencyHeatLattice : ChainLatticeOps<ResidencyHeatTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return ResidencyHeatTag::Cold;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return ResidencyHeatTag::Hot;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "ResidencyHeatLattice";
    }

    template <ResidencyHeatTag T>
    struct At {
        struct element_type {
            using residency_heat_tag_value_type = ResidencyHeatTag;
            [[nodiscard]] constexpr operator residency_heat_tag_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr ResidencyHeatTag tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case ResidencyHeatTag::Cold: return "ResidencyHeatLattice::At<Cold>";
                case ResidencyHeatTag::Warm: return "ResidencyHeatLattice::At<Warm>";
                case ResidencyHeatTag::Hot:  return "ResidencyHeatLattice::At<Hot>";
                default:                     return "ResidencyHeatLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace residency_heat_tag {
    using ColdHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Cold>;
    using WarmHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Warm>;
    using HotHeat  = ResidencyHeatLattice::At<ResidencyHeatTag::Hot>;
}  // namespace residency_heat_tag

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::residency_heat_lattice_self_test {

static_assert(residency_heat_tag_count == 3,
    "ResidencyHeatTag catalog diverged from {Cold, Warm, Hot}; "
    "confirm intent and update the dispatcher's heat-tier admission "
    "gates + Augur per-axis heat-tracking plumbing.");

[[nodiscard]] consteval bool every_residency_heat_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ResidencyHeatTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (residency_heat_tag_name([:en:]) ==
            std::string_view{"<unknown ResidencyHeatTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_residency_heat_tag_has_name(),
    "residency_heat_tag_name() switch missing arm for at least one tier.");

static_assert(Lattice<ResidencyHeatLattice>);
static_assert(BoundedLattice<ResidencyHeatLattice>);
static_assert(Lattice<residency_heat_tag::ColdHeat>);
static_assert(Lattice<residency_heat_tag::WarmHeat>);
static_assert(Lattice<residency_heat_tag::HotHeat>);
static_assert(BoundedLattice<residency_heat_tag::HotHeat>);

static_assert(!UnboundedLattice<ResidencyHeatLattice>);
static_assert(!Semiring<ResidencyHeatLattice>);

static_assert(std::is_empty_v<residency_heat_tag::ColdHeat::element_type>);
static_assert(std::is_empty_v<residency_heat_tag::WarmHeat::element_type>);
static_assert(std::is_empty_v<residency_heat_tag::HotHeat::element_type>);

// EXHAUSTIVE coverage — (ResidencyHeatTag)³ = 27 triples.
static_assert(verify_chain_lattice_exhaustive<ResidencyHeatLattice>(),
    "ResidencyHeatLattice's chain-order axioms must hold at every "
    "(ResidencyHeatTag)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<ResidencyHeatLattice>(),
    "ResidencyHeatLattice's chain order must satisfy distributivity at "
    "every (ResidencyHeatTag)³ triple.");

// Direct order witnesses.
static_assert( ResidencyHeatLattice::leq(ResidencyHeatTag::Cold, ResidencyHeatTag::Warm));
static_assert( ResidencyHeatLattice::leq(ResidencyHeatTag::Warm, ResidencyHeatTag::Hot));
static_assert( ResidencyHeatLattice::leq(ResidencyHeatTag::Cold, ResidencyHeatTag::Hot)); // transitive
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Hot,  ResidencyHeatTag::Cold));
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Hot,  ResidencyHeatTag::Warm));
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Warm, ResidencyHeatTag::Cold));

static_assert(ResidencyHeatLattice::bottom() == ResidencyHeatTag::Cold);
static_assert(ResidencyHeatLattice::top()    == ResidencyHeatTag::Hot);

static_assert(ResidencyHeatLattice::join(ResidencyHeatTag::Cold, ResidencyHeatTag::Hot)
              == ResidencyHeatTag::Hot);
static_assert(ResidencyHeatLattice::join(ResidencyHeatTag::Warm, ResidencyHeatTag::Cold)
              == ResidencyHeatTag::Warm);
static_assert(ResidencyHeatLattice::meet(ResidencyHeatTag::Cold, ResidencyHeatTag::Hot)
              == ResidencyHeatTag::Cold);
static_assert(ResidencyHeatLattice::meet(ResidencyHeatTag::Warm, ResidencyHeatTag::Hot)
              == ResidencyHeatTag::Warm);

static_assert(ResidencyHeatLattice::name() == "ResidencyHeatLattice");
static_assert(residency_heat_tag::ColdHeat::name() == "ResidencyHeatLattice::At<Cold>");
static_assert(residency_heat_tag::WarmHeat::name() == "ResidencyHeatLattice::At<Warm>");
static_assert(residency_heat_tag::HotHeat::name()  == "ResidencyHeatLattice::At<Hot>");

[[nodiscard]] consteval bool every_at_residency_heat_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ResidencyHeatTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ResidencyHeatLattice::At<([:en:])>::name() ==
            std::string_view{"ResidencyHeatLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_residency_heat_tag_has_name(),
    "ResidencyHeatLattice::At<T>::name() switch missing an arm.");

static_assert(residency_heat_tag::ColdHeat::tier == ResidencyHeatTag::Cold);
static_assert(residency_heat_tag::WarmHeat::tier == ResidencyHeatTag::Warm);
static_assert(residency_heat_tag::HotHeat::tier  == ResidencyHeatTag::Hot);

// ── Layout invariants ───────────────────────────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using HotGraded = Graded<ModalityKind::Absolute, residency_heat_tag::HotHeat, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, double);

template <typename T_>
using WarmGraded = Graded<ModalityKind::Absolute, residency_heat_tag::WarmHeat, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmGraded, EightByteValue);

template <typename T_>
using ColdGraded = Graded<ModalityKind::Absolute, residency_heat_tag::ColdHeat, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdGraded, EightByteValue);

inline void runtime_smoke_test() {
    ResidencyHeatTag a = ResidencyHeatTag::Cold;
    ResidencyHeatTag b = ResidencyHeatTag::Hot;
    [[maybe_unused]] bool             l1   = ResidencyHeatLattice::leq(a, b);
    [[maybe_unused]] ResidencyHeatTag j1   = ResidencyHeatLattice::join(a, b);
    [[maybe_unused]] ResidencyHeatTag m1   = ResidencyHeatLattice::meet(a, b);
    [[maybe_unused]] ResidencyHeatTag bot  = ResidencyHeatLattice::bottom();
    [[maybe_unused]] ResidencyHeatTag topv = ResidencyHeatLattice::top();

    ResidencyHeatTag warm = ResidencyHeatTag::Warm;
    [[maybe_unused]] ResidencyHeatTag j2 = ResidencyHeatLattice::join(warm, a);   // Warm
    [[maybe_unused]] ResidencyHeatTag m2 = ResidencyHeatLattice::meet(warm, b);   // Warm

    OneByteValue v{42};
    HotGraded<OneByteValue> initial{v, residency_heat_tag::HotHeat::bottom()};
    auto widened   = initial.weaken(residency_heat_tag::HotHeat::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(residency_heat_tag::HotHeat::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    residency_heat_tag::HotHeat::element_type e{};
    [[maybe_unused]] ResidencyHeatTag rec = e;
}

}  // namespace detail::residency_heat_lattice_self_test

}  // namespace crucible::algebra::lattices
