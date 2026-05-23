// FIXY-V-255 sentinel TU: safety/BarrierGuarded.h — the Graded<Absolute,
// BarrierStrengthLattice::At<Tier>, P> regime-1 carrier for the V-253
// BarrierStrength axis, plus safety/IsBarrierGuarded.h (concept
// extractor) and the row_hash_contribution<safety::BarrierGuarded<Tier,
// Inner>> federation-cache discriminator wired in
// safety/diag/RowHashFold.h (salt 0x2D).
//
// This TU forces every header-embedded static_assert to compile under
// the project warning flags (header-only static_asserts are otherwise
// unverified — see feedback_header_only_static_assert_blind_spot) AND
// adds the cross-cutting checks the wrapper header cannot self-contain:
// the §XVI DimensionTraits quadruple, row_hash distinctness +
// nesting-order sensitivity, the "× source composition" demonstration
// against the existing Tagged<T, Source> machinery (the slot V-261's
// source::ArchPinned<Arch> will occupy), and the runtime smoke tests.
//
// FLOOR semantics is the DUAL of V-254 Hw's ceiling: a value satisfies a
// consumer requiring minimum ordering iff its published fence is ⊒ that
// floor; weaken<Lower>() moves DOWN the chain.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - test/safety_neg/neg_barrier_consumer_too_weak.cpp  (floor not met)
//   - test/safety_neg/neg_barrier_strengthen_up.cpp      (chain direction)

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/IsBarrierGuarded.h>
#include <crucible/safety/Hw.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using Bs_t = sf::BarrierStrength_v;

// ── Regime-1 sizeof preservation (the V-255 sentinel assert) ───────
static_assert(sizeof(sf::BarrierGuarded<Bs_t::None,      int>)    == sizeof(int));
static_assert(sizeof(sf::BarrierGuarded<Bs_t::AcqRel,    double>) == sizeof(double));
static_assert(sizeof(sf::BarrierGuarded<Bs_t::FullFence, char>)   == sizeof(char));

// ── FLOOR subsumption (HS14 positive): SeqCst admissible at an AcqRel
//    floor; AcquireLoad NOT admissible at an AcqRel floor.
static_assert( sf::BarrierGuarded<Bs_t::SeqCst, int>::satisfies<Bs_t::AcqRel>);
static_assert(!sf::BarrierGuarded<Bs_t::AcquireLoad, int>::satisfies<Bs_t::AcqRel>);

// ── §XVI DimensionTraits quadruple — BarrierGuarded maps to the
//    BarrierStrength axis, Tier-S Semiring, Absolute modality.
static_assert(sf::DimensionedGradedWrapper<sf::BarrierGuarded<Bs_t::AcqRel, int>>);
static_assert(sf::wrapper_dimension_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
              == sf::DimensionAxis::BarrierStrength);
static_assert(sf::wrapper_tier_v<sf::BarrierGuarded<Bs_t::AcqRel, int>> == sf::TierKind::Semiring);
static_assert(sf::wrapper_modality_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::BarrierGuarded<Bs_t::SeqCst, int>>(),
    "BarrierGuarded<> must satisfy the GAPS-091 (lattice, modality, tier, axis) quadruple.");

// ── IsBarrierGuarded concept extractor ──────────────────────────────
static_assert(ex::IsBarrierGuarded<sf::BarrierGuarded<Bs_t::AcqRel, int>>);
static_assert(!ex::IsBarrierGuarded<int>);
static_assert(std::is_same_v<ex::barrier_guarded_value_t<sf::BarrierGuarded<Bs_t::SeqCst, double>>, double>);
static_assert(ex::barrier_guarded_tier_v<sf::BarrierGuarded<Bs_t::FullFence, int>> == Bs_t::FullFence);

// ── row_hash distinctness — different tiers, different payloads ─────
static_assert(dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
              != dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::SeqCst, int>>,
    "BarrierGuarded<AcqRel,int> and <SeqCst,int> MUST hash differently — "
    "the tier salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
              != dg::row_hash_contribution_v<int>,
    "BarrierGuarded<AcqRel,int> MUST hash differently from bare int — the "
    "wrapper tag (0x2D) discriminates the wrapped value.");

// ── Distinct salt vs the sister V-254 Hw wrapper at the SAME ordinal —
//    BarrierGuarded<tier=4> and Hw<tier=4> share an underlying enum value
//    but MUST hash differently because their wrapper salts differ
//    (0x2D vs 0x2C).  Without distinct salts the two axes would collide.
using HwInstr_t = sf::HwInstruction_v;
static_assert(static_cast<std::uint8_t>(Bs_t::AcqRel)
              == static_cast<std::uint8_t>(HwInstr_t::PrivilegedMsr),
    "Test premise: both enums use ordinal 4 — proves the salt, not the "
    "ordinal, is what discriminates the two axes.");
static_assert(dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
              != dg::row_hash_contribution_v<sf::Hw<HwInstr_t::PrivilegedMsr, int>>,
    "BarrierGuarded and Hw at the same enum ordinal MUST hash differently "
    "— the per-wrapper salt (0x2D vs 0x2C) discriminates the axes.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) — Hw⊃BarrierGuarded is a
//    different federation-cache slot than BarrierGuarded⊃Hw.
static_assert(
    dg::row_hash_contribution_v<sf::Hw<HwInstr_t::Scalar, sf::BarrierGuarded<Bs_t::AcqRel, int>>>
    != dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, sf::Hw<HwInstr_t::Scalar, int>>>,
    "Hw<Scalar, BarrierGuarded<AcqRel,int>> and BarrierGuarded<AcqRel, "
    "Hw<Scalar,int>> MUST hash differently — row_hash is order-sensitive "
    "per the canonical wrapper-nesting discipline.");

// ── "× source composition" — the V-255 premise, demonstrated against
//    the EXISTING Tagged<T, Source> machinery (the slot V-261's
//    source::ArchPinned<Arch> will occupy with zero churn).  A barrier-
//    guarded value further pinned to a provenance composes cleanly, and
//    the composition is order-sensitive in the federation cache.
using AcqRelInt = sf::BarrierGuarded<Bs_t::AcqRel, int>;
using TaggedOuter = sf::Tagged<AcqRelInt, sf::source::FromInternal>;
using TaggedInner = sf::BarrierGuarded<Bs_t::AcqRel, sf::Tagged<int, sf::source::FromInternal>>;
static_assert(dg::row_hash_contribution_v<TaggedOuter>
              != dg::row_hash_contribution_v<TaggedInner>,
    "Tagged<BarrierGuarded<AcqRel,int>, source> and BarrierGuarded<AcqRel, "
    "Tagged<int, source>> MUST hash differently — the BarrierGuarded × "
    "source composition is order-sensitive, so V-261's source::ArchPinned "
    "slots into the identical nest with a distinct, correct cache key.");
static_assert(dg::row_hash_contribution_v<TaggedOuter>
              != dg::row_hash_contribution_v<AcqRelInt>,
    "Adding a source tag MUST change the federation-cache slot — the "
    "barrier-guarded value alone and the arch-pinned-then-barrier-guarded "
    "value are distinct compositions.");

}  // namespace

int main() {
    ::crucible::safety::detail::barrier_guarded_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_barrier_guarded_smoke_test()) return 1;
    return 0;
}
