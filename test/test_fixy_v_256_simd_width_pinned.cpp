// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and adds the cross-wrapper checks no
// single wrapper header can state about itself.

#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/IsSimdWidthPinned.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Si_t = sf::SimdIsa_v;

static_assert(sizeof(sf::SimdWidthPinned<Si_t::Scalar, int>) == sizeof(int));
static_assert(sizeof(sf::SimdWidthPinned<Si_t::Avx2, double>) == sizeof(double));
static_assert(sizeof(sf::SimdWidthPinned<Si_t::Portable, char>) == sizeof(char));

static_assert(sf::SimdWidthPinned<Si_t::Avx512Bw, int>::satisfies<Si_t::Avx2>);
static_assert(!sf::SimdWidthPinned<Si_t::Avx2, int>::satisfies<Si_t::Avx512Bw>);
static_assert(!sf::SimdWidthPinned<Si_t::Avx2, int>::satisfies<Si_t::Neon>);

static_assert(sf::DimensionedGradedWrapper<sf::SimdWidthPinned<Si_t::Avx2, int>>);
static_assert(sf::wrapper_dimension_v<sf::SimdWidthPinned<Si_t::Avx2, int>> == sf::DimensionAxis::SimdIsa);
static_assert(sf::wrapper_tier_v<sf::SimdWidthPinned<Si_t::Avx2, int>> == sf::TierKind::Lattice,
              "SimdIsa is a Tier-L Lattice axis, not a Tier-S Semiring axis.");
static_assert(sf::wrapper_modality_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::SimdWidthPinned<Si_t::Avx512Bw, int>>(),
              "SimdWidthPinned<> must satisfy the (lattice, modality, tier, axis) quadruple.");

static_assert(ex::IsSimdWidthPinned<sf::SimdWidthPinned<Si_t::Avx2, int>>);
static_assert(!ex::IsSimdWidthPinned<int>);
static_assert(std::is_same_v<ex::simd_width_pinned_value_t<sf::SimdWidthPinned<Si_t::Avx512Bw, double>>, double>);
static_assert(ex::simd_width_pinned_isa_v<sf::SimdWidthPinned<Si_t::Neon, int>> == Si_t::Neon);

static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
                  != dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx512Bw, int>>,
              "SimdWidthPinned<Avx2,int> and <Avx512Bw,int> MUST hash differently — "
              "the ISA salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>> != dg::row_hash_contribution_v<int>,
              "SimdWidthPinned<Avx2,int> MUST hash differently from bare int — the "
              "wrapper tag (0x2E) discriminates the wrapped value.");

static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
                  != dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Neon, int>>,
              "x86 Avx2 and ARM Neon are incomparable trunks — they MUST hash to "
              "distinct slots so a cross-trunk kernel never aliases an x86 cache key.");

using NvBackend = sf::VendorBackend_v;
static_assert(dg::row_hash_contribution_v<sf::SimdWidthPinned<Si_t::Avx2, int>>
                  != dg::row_hash_contribution_v<sf::Vendor<NvBackend::NV, int>>,
              "SimdWidthPinned and Vendor are distinct Repr-neighborhood wrappers — "
              "their per-wrapper salts MUST discriminate the axes.");

static_assert(dg::row_hash_contribution_v<sf::Vendor<NvBackend::NV, sf::SimdWidthPinned<Si_t::Avx2, int>>>
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
