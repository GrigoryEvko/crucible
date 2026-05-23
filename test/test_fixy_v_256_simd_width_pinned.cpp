// FIXY-V-256 sentinel TU: safety/SimdWidthPinned.h — the Graded<Absolute,
// SimdIsaLattice::At<W>, P> regime-1 carrier for the V-253 SimdIsa axis,
// plus safety/IsSimdWidthPinned.h (concept extractor) and the
// row_hash_contribution<safety::SimdWidthPinned<W, Inner>> federation-
// cache discriminator wired in safety/diag/RowHashFold.h (salt 0x2E).
//
// This TU forces every header-embedded static_assert to compile under
// the project warning flags (header-only static_asserts are otherwise
// unverified — see feedback_header_only_static_assert_blind_spot) AND
// adds the cross-cutting checks the wrapper header cannot self-contain:
// the §XVI DimensionTraits quadruple (Tier-L LATTICE — distinct from the
// V-254/255 Tier-S Semiring siblings), row_hash distinctness +
// nesting-order sensitivity, the cross-trunk partial-order incomparability
// in row_hash, and the runtime smoke tests.
//
// SimdWidthPinned is the PARTIAL-ORDER provider sibling of Vendor:
// satisfies<R>=leq(R,W) (W subsumes requirement R), relax<Weaker>() DOWN.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - test/safety_neg/neg_simd_provider_too_weak.cpp       (within-trunk admission)
//   - test/safety_neg/neg_simd_relax_up_or_cross_trunk.cpp (partial-order direction)

#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/IsSimdWidthPinned.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using Si_t = sf::SimdIsa_v;

// ── Regime-1 sizeof preservation (the V-256 sentinel assert) ───────
static_assert(sizeof(sf::SimdWidthPinned<Si_t::Scalar,   int>)    == sizeof(int));
static_assert(sizeof(sf::SimdWidthPinned<Si_t::Avx2,     double>) == sizeof(double));
static_assert(sizeof(sf::SimdWidthPinned<Si_t::Portable, char>)   == sizeof(char));

// ── Partial-order subsumption (HS14 positive): Avx512Bw subsumes Avx2;
//    Avx2 does NOT subsume Avx512Bw; Avx2 incomparable with Neon.
static_assert( sf::SimdWidthPinned<Si_t::Avx512Bw, int>::satisfies<Si_t::Avx2>);
static_assert(!sf::SimdWidthPinned<Si_t::Avx2, int>::satisfies<Si_t::Avx512Bw>);
static_assert(!sf::SimdWidthPinned<Si_t::Avx2, int>::satisfies<Si_t::Neon>);

// ── §XVI DimensionTraits quadruple — SimdWidthPinned maps to the SimdIsa
//    axis, Tier-L LATTICE (NOT Semiring), Absolute modality.
static_assert(sf::DimensionedGradedWrapper<sf::SimdWidthPinned<Si_t::Avx2, int>>);
static_assert(sf::wrapper_dimension_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              == sf::DimensionAxis::SimdIsa);
static_assert(sf::wrapper_tier_v<sf::SimdWidthPinned<Si_t::Avx2, int>> == sf::TierKind::Lattice,
    "SimdIsa is a Tier-L Lattice axis — distinct from the V-254/255 "
    "Tier-S Semiring chain siblings.");
static_assert(sf::wrapper_modality_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::SimdWidthPinned<Si_t::Avx512Bw, int>>(),
    "SimdWidthPinned<> must satisfy the GAPS-091 (lattice, modality, tier, axis) quadruple.");

// ── IsSimdWidthPinned concept extractor ─────────────────────────────
static_assert(ex::IsSimdWidthPinned<sf::SimdWidthPinned<Si_t::Avx2, int>>);
static_assert(!ex::IsSimdWidthPinned<int>);
static_assert(std::is_same_v<ex::simd_width_pinned_value_t<sf::SimdWidthPinned<Si_t::Avx512Bw, double>>, double>);
static_assert(ex::simd_width_pinned_isa_v<sf::SimdWidthPinned<Si_t::Neon, int>> == Si_t::Neon);

// ── row_hash distinctness — different ISAs, different payloads ─────
static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              != dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx512Bw, int>>,
    "SimdWidthPinned<Avx2,int> and <Avx512Bw,int> MUST hash differently — "
    "the ISA salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              != dg::row_hash_contribution_v<int>,
    "SimdWidthPinned<Avx2,int> MUST hash differently from bare int — the "
    "wrapper tag (0x2E) discriminates the wrapped value.");

// ── Cross-trunk incomparable ISAs hash distinctly — the partial-order
//    signature.  Avx2 (x86) and Neon (ARM) are incomparable yet must
//    occupy separate federation-cache slots.
static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              != dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Neon, int>>,
    "x86 Avx2 and ARM Neon are incomparable trunks — they MUST hash to "
    "distinct slots so a cross-trunk kernel never aliases an x86 cache key.");

// ── Distinct salt vs the sister wrappers — SimdWidthPinned (0x2E) must
//    not collide with the Vendor (Representation-axis) wrapper.
using NvBackend = sf::VendorBackend_v;
static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              != dg::row_hash_contribution_v<sf::Vendor<NvBackend::NV, int>>,
    "SimdWidthPinned and Vendor are distinct Repr-neighborhood wrappers — "
    "their per-wrapper salts MUST discriminate the axes.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) — Vendor⊃SimdWidthPinned
//    is a different federation-cache slot than SimdWidthPinned⊃Vendor.
static_assert(
    dg::row_hash_contribution_v<sf::Vendor<NvBackend::NV, sf::SimdWidthPinned<Si_t::Avx2, int>>>
    != dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, sf::Vendor<NvBackend::NV, int>>>,
    "Vendor<NV, SimdWidthPinned<Avx2,int>> and SimdWidthPinned<Avx2, "
    "Vendor<NV,int>> MUST hash differently — row_hash is order-sensitive "
    "per the canonical wrapper-nesting discipline.");

}  // namespace

int main() {
    ::crucible::safety::detail::simd_width_pinned_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_simd_width_pinned_smoke_test()) return 1;
    return 0;
}
