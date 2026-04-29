#pragma once

// ── crucible::algebra::lattices::CipherTierLattice ──────────────────
//
// Three-tier total-order chain lattice over the Cipher persistence
// residency spectrum.  The grading axis underlying the CipherTier
// wrapper from 28_04_2026_effects.md §4.3.7.
//
// Citation: CRUCIBLE.md §L14 (Cipher three-tier persistence).
//
// THE LOAD-BEARING USE CASE: production tier-pinning at Cipher's
// publish/flush boundary.  `Cipher::publish_hot` returns
// CipherTier<Hot, T>; `publish_warm` returns CipherTier<Warm, T>;
// `flush_cold` returns CipherTier<Cold, T>.  A function declared
// `requires CipherTier::satisfies<Warm>` rejects callees carrying
// `CipherTierTag_v::Cold` at compile time — replacing today's
// review-only enforcement.  Augur's drift-attribution reads the
// tier to distinguish "cold-tier S3 latency" from "hot-path issue".
//
// Composes orthogonally with the six sister chain wrappers (DetSafe
// / HotPath / Wait / MemOrder / Progress / AllocClass) via wrapper-
// nesting per 28_04 §4.7.  CipherTier captures STORAGE RESIDENCY,
// distinct from HotPath's EXECUTION BUDGET — same 3-tier shape but
// orthogonal semantic axes.
//
// ── The classification ──────────────────────────────────────────────
//
//     Hot   — Lives in another Relay's RAM (RAID-style replicated
//              hot tier per CRUCIBLE.md §L14).  Single-node failure
//              recovery: ~zero-cost (fellow Relays already have
//              shards from the RAID redundancy).  ~5-100μs gossip
//              + memory copy.  The strongest persistence claim —
//              fastest recovery.  Production: CRUCIBLE_DAG_CHAIN
//              event-sourced steps; latest weight snapshots.
//     Warm  — Lives on local NVMe per Relay (1/N FSDP shard).
//              Recovery from reboot: seconds.  Survives the Relay
//              process dying, doesn't survive disk failure.
//              Production: per-iteration weight checkpoints; warm
//              KernelCache slabs.
//     Cold  — Lives in durable storage (S3/GCS/blob).  Recovery
//              from total cluster failure: minutes (download +
//              decompress + restore).  Survives any single-cluster
//              catastrophe.  The weakest persistence claim — slowest
//              recovery, most permissive admission.  Production:
//              cold-tier KernelCache eviction; long-term
//              CRUCIBLE_DAG_CHAIN archive; periodic snapshot
//              shipping.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class CipherTierTag ∈ {Cold, Warm, Hot}.
// Order:   Cold ⊑ Warm ⊑ Hot.
//
// Bottom = Cold (slowest recovery; satisfies only Cold-tolerating
//                consumers — i.e., S3-latency-OK contexts).
// Top    = Hot  (fastest recovery; satisfies every consumer).
// Join   = max  (the more-restricted of two providers).
// Meet   = min  (the more-permissive of two providers).
//
// ── Direction convention ────────────────────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-budget consumer is satisfied by a stronger-budget
// provider".  A Hot-tier value is admissible everywhere because
// it's the fastest-recovering claim possible.
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.7 SPEC ──────────────
//
// Spec puts Hot=0 ... Cold=2 (cheaper-to-recover ordinal).  This
// implementation INVERTS to Cold=0 ... Hot=2 to keep the chain
// direction uniform with the six sister chain lattices.  The
// SEMANTIC contract from the spec ("Augur reads tier for drift
// attribution") is preserved exactly:
//
//   CipherTier<Hot>::satisfies<Warm>  = leq(Warm, Hot)  = true ✓
//   CipherTier<Cold>::satisfies<Hot>  = leq(Hot, Cold)  = false ✓
//   CipherTier<Hot>::satisfies<Cold>  = leq(Cold, Hot)  = true ✓
//
// Same shape as HotPathLattice (also 3 tiers, also Hot-at-top,
// also inverted from spec); the two lattices are SEMANTICALLY
// distinct (CipherTier = storage residency, HotPath = execution
// budget) but structurally identical.
//
//   Axiom coverage:
//     TypeSafe — CipherTierTag is a strong scoped enum;
//                cross-tier mixing requires
//                `std::to_underlying`.
//   Runtime cost:
//     leq / join / meet — single integer compare; the three-element
//     domain compiles to a 1-byte field.  When wrapped at a fixed
//     type-level tier via `CipherTierLattice::At<CipherTierTag::Hot>`,
//     the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the six sister chain lattices: a per-CipherTierTag
// singleton sub-lattice with empty element_type.
//
// See FOUND-G43 (this file) for the lattice; FOUND-G44
// (safety/CipherTier.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.7 for the production-call-site
// rationale; CRUCIBLE.md §L14 for the Cipher three-tier persistence
// architecture.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── CipherTierTag — chain over Cipher persistence residency ────────
//
// Ordinal convention: Cold=0 (bottom) ... Hot=2 (top), per project
// convention (bottom=0=weakest).  INVERTS the spec's ordinal hint;
// semantic contract preserved.  Same shape as HotPathTier but the
// type is DISTINCT (different scoped enum class) — see lattice
// docblock for the orthogonal-axis rationale.
enum class CipherTierTag : std::uint8_t {
    Cold = 0,    // bottom: S3/GCS durable storage (~minutes recovery)
    Warm = 1,    // local NVMe per Relay (~seconds recovery)
    Hot  = 2,    // top: another Relay's RAM (~μs recovery)
};

inline constexpr std::size_t cipher_tier_tag_count =
    std::meta::enumerators_of(^^CipherTierTag).size();

[[nodiscard]] consteval std::string_view cipher_tier_tag_name(CipherTierTag t) noexcept {
    switch (t) {
        case CipherTierTag::Cold: return "Cold";
        case CipherTierTag::Warm: return "Warm";
        case CipherTierTag::Hot:  return "Hot";
        default:                  return std::string_view{"<unknown CipherTierTag>"};
    }
}

// ── Full CipherTierLattice (chain order) ────────────────────────────
struct CipherTierLattice : ChainLatticeOps<CipherTierTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return CipherTierTag::Cold;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return CipherTierTag::Hot;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "CipherTierLattice";
    }

    template <CipherTierTag T>
    struct At {
        struct element_type {
            using cipher_tier_tag_value_type = CipherTierTag;
            [[nodiscard]] constexpr operator cipher_tier_tag_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr CipherTierTag tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case CipherTierTag::Cold: return "CipherTierLattice::At<Cold>";
                case CipherTierTag::Warm: return "CipherTierLattice::At<Warm>";
                case CipherTierTag::Hot:  return "CipherTierLattice::At<Hot>";
                default:                  return "CipherTierLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace cipher_tier_tag {
    using ColdTier = CipherTierLattice::At<CipherTierTag::Cold>;
    using WarmTier = CipherTierLattice::At<CipherTierTag::Warm>;
    using HotTier  = CipherTierLattice::At<CipherTierTag::Hot>;
}  // namespace cipher_tier_tag

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::cipher_tier_lattice_self_test {

static_assert(cipher_tier_tag_count == 3,
    "CipherTierTag catalog diverged from {Cold, Warm, Hot}; confirm "
    "intent and update the dispatcher's persistence-tier admission "
    "gates + Augur drift-attribution plumbing.");

[[nodiscard]] consteval bool every_cipher_tier_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CipherTierTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cipher_tier_tag_name([:en:]) ==
            std::string_view{"<unknown CipherTierTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cipher_tier_tag_has_name(),
    "cipher_tier_tag_name() switch missing arm for at least one tier.");

static_assert(Lattice<CipherTierLattice>);
static_assert(BoundedLattice<CipherTierLattice>);
static_assert(Lattice<cipher_tier_tag::ColdTier>);
static_assert(Lattice<cipher_tier_tag::WarmTier>);
static_assert(Lattice<cipher_tier_tag::HotTier>);
static_assert(BoundedLattice<cipher_tier_tag::HotTier>);

static_assert(!UnboundedLattice<CipherTierLattice>);
static_assert(!Semiring<CipherTierLattice>);

static_assert(std::is_empty_v<cipher_tier_tag::ColdTier::element_type>);
static_assert(std::is_empty_v<cipher_tier_tag::WarmTier::element_type>);
static_assert(std::is_empty_v<cipher_tier_tag::HotTier::element_type>);

// EXHAUSTIVE coverage — (CipherTierTag)³ = 27 triples.
static_assert(verify_chain_lattice_exhaustive<CipherTierLattice>(),
    "CipherTierLattice's chain-order axioms must hold at every "
    "(CipherTierTag)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<CipherTierLattice>(),
    "CipherTierLattice's chain order must satisfy distributivity at "
    "every (CipherTierTag)³ triple.");

// Direct order witnesses.
static_assert( CipherTierLattice::leq(CipherTierTag::Cold, CipherTierTag::Warm));
static_assert( CipherTierLattice::leq(CipherTierTag::Warm, CipherTierTag::Hot));
static_assert( CipherTierLattice::leq(CipherTierTag::Cold, CipherTierTag::Hot)); // transitive
static_assert(!CipherTierLattice::leq(CipherTierTag::Hot,  CipherTierTag::Cold));
static_assert(!CipherTierLattice::leq(CipherTierTag::Hot,  CipherTierTag::Warm));
static_assert(!CipherTierLattice::leq(CipherTierTag::Warm, CipherTierTag::Cold));

static_assert(CipherTierLattice::bottom() == CipherTierTag::Cold);
static_assert(CipherTierLattice::top()    == CipherTierTag::Hot);

static_assert(CipherTierLattice::join(CipherTierTag::Cold, CipherTierTag::Hot)
              == CipherTierTag::Hot);
static_assert(CipherTierLattice::join(CipherTierTag::Warm, CipherTierTag::Cold)
              == CipherTierTag::Warm);
static_assert(CipherTierLattice::meet(CipherTierTag::Cold, CipherTierTag::Hot)
              == CipherTierTag::Cold);
static_assert(CipherTierLattice::meet(CipherTierTag::Warm, CipherTierTag::Hot)
              == CipherTierTag::Warm);

static_assert(CipherTierLattice::name() == "CipherTierLattice");
static_assert(cipher_tier_tag::ColdTier::name() == "CipherTierLattice::At<Cold>");
static_assert(cipher_tier_tag::WarmTier::name() == "CipherTierLattice::At<Warm>");
static_assert(cipher_tier_tag::HotTier::name()  == "CipherTierLattice::At<Hot>");

[[nodiscard]] consteval bool every_at_cipher_tier_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CipherTierTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (CipherTierLattice::At<([:en:])>::name() ==
            std::string_view{"CipherTierLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_cipher_tier_tag_has_name(),
    "CipherTierLattice::At<T>::name() switch missing an arm.");

static_assert(cipher_tier_tag::ColdTier::tier == CipherTierTag::Cold);
static_assert(cipher_tier_tag::WarmTier::tier == CipherTierTag::Warm);
static_assert(cipher_tier_tag::HotTier::tier  == CipherTierTag::Hot);

// ── Layout invariants ───────────────────────────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using HotGraded = Graded<ModalityKind::Absolute, cipher_tier_tag::HotTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, double);

template <typename T_>
using WarmGraded = Graded<ModalityKind::Absolute, cipher_tier_tag::WarmTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmGraded, EightByteValue);

template <typename T_>
using ColdGraded = Graded<ModalityKind::Absolute, cipher_tier_tag::ColdTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdGraded, EightByteValue);

inline void runtime_smoke_test() {
    CipherTierTag a = CipherTierTag::Cold;
    CipherTierTag b = CipherTierTag::Hot;
    [[maybe_unused]] bool          l1   = CipherTierLattice::leq(a, b);
    [[maybe_unused]] CipherTierTag j1   = CipherTierLattice::join(a, b);
    [[maybe_unused]] CipherTierTag m1   = CipherTierLattice::meet(a, b);
    [[maybe_unused]] CipherTierTag bot  = CipherTierLattice::bottom();
    [[maybe_unused]] CipherTierTag topv = CipherTierLattice::top();

    CipherTierTag warm = CipherTierTag::Warm;
    [[maybe_unused]] CipherTierTag j2 = CipherTierLattice::join(warm, a);   // Warm
    [[maybe_unused]] CipherTierTag m2 = CipherTierLattice::meet(warm, b);   // Warm

    OneByteValue v{42};
    HotGraded<OneByteValue> initial{v, cipher_tier_tag::HotTier::bottom()};
    auto widened   = initial.weaken(cipher_tier_tag::HotTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(cipher_tier_tag::HotTier::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    cipher_tier_tag::HotTier::element_type e{};
    [[maybe_unused]] CipherTierTag rec = e;
}

}  // namespace detail::cipher_tier_lattice_self_test

}  // namespace crucible::algebra::lattices
