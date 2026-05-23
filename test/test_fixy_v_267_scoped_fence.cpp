// FIXY-V-267 sentinel TU: safety/ScopedFence.h — the Graded<Absolute,
// MemoryScopeLattice::At<S>, P> regime-1 carrier for the V-266 MemoryScope
// axis, plus safety/IsScopedFence.h (concept extractor) and the
// row_hash_contribution<safety::ScopedFence<S, Inner>> federation-cache
// discriminator wired in safety/diag/RowHashFold.h (salt 0x2F).
//
// This TU forces every header-embedded static_assert to compile under
// the project warning flags (header-only static_asserts are otherwise
// unverified — see feedback_header_only_static_assert_blind_spot) AND
// adds the cross-cutting checks the wrapper header cannot self-contain:
// the §XVI DimensionTraits quadruple (Tier-L LATTICE — like the V-256
// SimdWidthPinned sibling), row_hash distinctness + nesting-order
// sensitivity, the cross-trunk partial-order incomparability in row_hash,
// and the runtime smoke tests.
//
// ScopedFence is the PARTIAL-ORDER provider sibling of SimdWidthPinned:
// satisfies<R>=leq(R,S) (publish scope S subsumes requirement R),
// relax<Narrower>() DOWN the partial order.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - test/safety_neg/neg_scoped_fence_provider_too_narrow.cpp   (within-trunk admission)
//   - test/safety_neg/neg_scoped_fence_relax_up_or_cross_trunk.cpp (partial-order direction)

#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/IsScopedFence.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using Ms_t = sf::MemoryScope_v;
using Si_t = sf::SimdIsa_v;

// ── Regime-1 sizeof preservation (the V-267 sentinel assert) ───────
static_assert(sizeof(sf::ScopedFence<Ms_t::Thread, int>)    == sizeof(int));
static_assert(sizeof(sf::ScopedFence<Ms_t::Cta,    double>) == sizeof(double));
static_assert(sizeof(sf::ScopedFence<Ms_t::System, char>)   == sizeof(char));

// ── Partial-order subsumption (HS14 positive): Gpu subsumes Cta;
//    Cta does NOT subsume Gpu; Cta incomparable with Inner.
static_assert( sf::ScopedFence<Ms_t::Gpu, int>::satisfies<Ms_t::Cta>);
static_assert(!sf::ScopedFence<Ms_t::Cta, int>::satisfies<Ms_t::Gpu>);
static_assert(!sf::ScopedFence<Ms_t::Cta, int>::satisfies<Ms_t::Inner>);

// ── §XVI DimensionTraits quadruple — ScopedFence maps to the MemoryScope
//    axis, Tier-L LATTICE (NOT Semiring), Absolute modality.
static_assert(sf::DimensionedGradedWrapper<sf::ScopedFence<Ms_t::Cta, int>>);
static_assert(sf::wrapper_dimension_v<sf::ScopedFence<Ms_t::Cta, int>>
              == sf::DimensionAxis::MemoryScope);
static_assert(sf::wrapper_tier_v<sf::ScopedFence<Ms_t::Cta, int>> == sf::TierKind::Lattice,
    "MemoryScope is a Tier-L Lattice axis — the partial-order sibling of "
    "the V-256 SimdIsa axis.");
static_assert(sf::wrapper_modality_v<sf::ScopedFence<Ms_t::Cta, int>>
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::ScopedFence<Ms_t::Gpu, int>>(),
    "ScopedFence<> must satisfy the GAPS-091 (lattice, modality, tier, axis) quadruple.");

// ── IsScopedFence concept extractor ─────────────────────────────────
static_assert(ex::IsScopedFence<sf::ScopedFence<Ms_t::Cta, int>>);
static_assert(!ex::IsScopedFence<int>);
static_assert(std::is_same_v<ex::scoped_fence_value_t<sf::ScopedFence<Ms_t::Gpu, double>>, double>);
static_assert(ex::scoped_fence_scope_v<sf::ScopedFence<Ms_t::Inner, int>> == Ms_t::Inner);

// ── row_hash distinctness — different scopes, different payloads ───
static_assert(dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, int>>
              != dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Gpu, int>>,
    "ScopedFence<Cta,int> and <Gpu,int> MUST hash differently — the scope "
    "salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, int>>
              != dg::row_hash_contribution_v<int>,
    "ScopedFence<Cta,int> MUST hash differently from bare int — the wrapper "
    "tag (0x2F) discriminates the wrapped value.");

// ── Cross-trunk incomparable scopes hash distinctly — the partial-order
//    signature.  Cta (accel) and Inner (ARM) are incomparable yet must
//    occupy separate federation-cache slots.
static_assert(dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, int>>
              != dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Inner, int>>,
    "accel Cta and ARM Inner are incomparable trunks — they MUST hash to "
    "distinct slots so a cross-trunk fence never aliases an accel cache key.");

// ── Distinct salt vs the sister wrapper — ScopedFence (0x2F) must not
//    collide with SimdWidthPinned (0x2E), the other Tier-L Lattice axis.
static_assert(dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, int>>
              != dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>,
    "ScopedFence and SimdWidthPinned are distinct Repr-neighborhood Tier-L "
    "wrappers — their per-wrapper salts MUST discriminate the axes.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) — SimdWidthPinned⊃ScopedFence
//    is a different federation-cache slot than ScopedFence⊃SimdWidthPinned.
static_assert(
    dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, sf::ScopedFence<Ms_t::Cta, int>>>
    != dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, sf::SimdWidthPinned<Si_t::Avx2, int>>>,
    "SimdWidthPinned<Avx2, ScopedFence<Cta,int>> and ScopedFence<Cta, "
    "SimdWidthPinned<Avx2,int>> MUST hash differently — row_hash is "
    "order-sensitive per the canonical wrapper-nesting discipline.");

}  // namespace

int main() {
    ::crucible::safety::detail::scoped_fence_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_scoped_fence_smoke_test()) return 1;
    return 0;
}
