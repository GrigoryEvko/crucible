// FIXY-V-254 sentinel TU: safety/Hw.h — the Graded<Absolute,
// HwInstructionLattice::At<Tier>, P> regime-1 carrier for the V-253
// HwInstruction axis, plus safety/IsHw.h (concept extractor) and the
// row_hash_contribution<safety::Hw<Tier, Inner>> federation-cache
// discriminator wired in safety/diag/RowHashFold.h (salt 0x2C).
//
// This TU forces every header-embedded static_assert to compile under
// the project warning flags (header-only static_asserts are otherwise
// unverified — see feedback_header_only_static_assert_blind_spot) AND
// adds the cross-cutting checks the wrapper header cannot self-contain:
// the §XVI DimensionTraits quadruple, row_hash distinctness +
// nesting-order sensitivity, and the runtime smoke tests.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - test/safety_neg/neg_hw_consumer_too_weak.cpp  (admission gate)
//   - test/safety_neg/neg_hw_widen_to_lower.cpp     (chain direction)

#include <crucible/safety/Hw.h>
#include <crucible/safety/IsHw.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using Hw_t = sf::HwInstruction_v;

// ── Regime-1 sizeof preservation (the V-254 sentinel assert) ───────
static_assert(sizeof(sf::Hw<Hw_t::Scalar, int>)              == sizeof(int));
static_assert(sizeof(sf::Hw<Hw_t::Vectorizable, double>)     == sizeof(double));
static_assert(sizeof(sf::Hw<Hw_t::PrivilegedMsr, char>)      == sizeof(char));

// ── Subsumption (HS14 positive): Scalar admissible at a Vectorizable
//    ceiling; PrivilegedMsr NOT admissible at a NonDeterministicTsc one.
static_assert( sf::Hw<Hw_t::Scalar, int>::satisfies<Hw_t::Vectorizable>);
static_assert(!sf::Hw<Hw_t::PrivilegedMsr, int>::satisfies<Hw_t::NonDeterministicTsc>);

// ── §XVI DimensionTraits quadruple — Hw maps to the HwInstruction axis,
//    Tier-S Semiring, Absolute modality, lattice = HwInstructionLattice.
static_assert(sf::DimensionedGradedWrapper<sf::Hw<Hw_t::Scalar, int>>);
static_assert(sf::wrapper_dimension_v<sf::Hw<Hw_t::Scalar, int>>
              == sf::DimensionAxis::HwInstruction);
static_assert(sf::wrapper_tier_v<sf::Hw<Hw_t::Scalar, int>> == sf::TierKind::Semiring);
static_assert(sf::wrapper_modality_v<sf::Hw<Hw_t::Scalar, int>>
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::Hw<Hw_t::Vectorizable, int>>(),
    "Hw<> must satisfy the GAPS-091 (lattice, modality, tier, axis) quadruple.");

// ── IsHw concept extractor ─────────────────────────────────────────
static_assert(ex::IsHw<sf::Hw<Hw_t::Scalar, int>>);
static_assert(!ex::IsHw<int>);
static_assert(std::is_same_v<ex::hw_value_t<sf::Hw<Hw_t::Vectorizable, double>>, double>);
static_assert(ex::hw_tier_v<sf::Hw<Hw_t::PrivilegedMsr, int>> == Hw_t::PrivilegedMsr);

// ── row_hash distinctness — different tiers, different payloads ─────
static_assert(dg::row_hash_contribution_v<sf::Hw<Hw_t::Scalar, int>>
              != dg::row_hash_contribution_v<sf::Hw<Hw_t::Vectorizable, int>>,
    "Hw<Scalar,int> and Hw<Vectorizable,int> MUST hash differently — "
    "the tier salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::Hw<Hw_t::Scalar, int>>
              != dg::row_hash_contribution_v<int>,
    "Hw<Scalar,int> MUST hash differently from bare int — the wrapper "
    "tag (0x2C) discriminates the wrapped value.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) — Vendor⊃Hw is a
//    different federation-cache slot than Hw⊃Vendor.
using NvBackend = sf::VendorBackend_v;
static_assert(
    dg::row_hash_contribution_v<sf::Vendor<NvBackend::NV, sf::Hw<Hw_t::Scalar, int>>>
    != dg::row_hash_contribution_v<sf::Hw<Hw_t::Scalar, sf::Vendor<NvBackend::NV, int>>>,
    "Vendor<NV, Hw<Scalar,int>> and Hw<Scalar, Vendor<NV,int>> MUST hash "
    "differently — row_hash is order-sensitive per the canonical "
    "wrapper-nesting discipline.");

}  // namespace

int main() {
    ::crucible::safety::detail::hw_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_hw_smoke_test()) return 1;
    return 0;
}
