// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and adds the cross-wrapper checks no
// single wrapper header can state about itself.

#include <crucible/safety/Hw.h>
#include <crucible/safety/IsHw.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Hw_t = sf::HwInstruction_v;

static_assert(sizeof(sf::Hw<Hw_t::Scalar, int>) == sizeof(int));
static_assert(sizeof(sf::Hw<Hw_t::Vectorizable, double>) == sizeof(double));
static_assert(sizeof(sf::Hw<Hw_t::PrivilegedMsr, char>) == sizeof(char));

static_assert(sf::Hw<Hw_t::Scalar, int>::satisfies<Hw_t::Vectorizable>);
static_assert(!sf::Hw<Hw_t::PrivilegedMsr, int>::satisfies<Hw_t::NonDeterministicTsc>);

static_assert(sf::DimensionedGradedWrapper<sf::Hw<Hw_t::Scalar, int>>);
static_assert(sf::wrapper_dimension_v<sf::Hw<Hw_t::Scalar, int>> == sf::DimensionAxis::HwInstruction);
static_assert(sf::wrapper_tier_v<sf::Hw<Hw_t::Scalar, int>> == sf::TierKind::Semiring);
static_assert(sf::wrapper_modality_v<sf::Hw<Hw_t::Scalar, int>> == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::Hw<Hw_t::Vectorizable, int>>(),
              "Hw<> must satisfy the (lattice, modality, tier, axis) quadruple.");

static_assert(ex::IsHw<sf::Hw<Hw_t::Scalar, int>>);
static_assert(!ex::IsHw<int>);
static_assert(std::is_same_v<ex::hw_value_t<sf::Hw<Hw_t::Vectorizable, double>>, double>);
static_assert(ex::hw_tier_v<sf::Hw<Hw_t::PrivilegedMsr, int>> == Hw_t::PrivilegedMsr);

static_assert(dg::row_hash_contribution_v<sf::Hw<Hw_t::Scalar, int>>
                  != dg::row_hash_contribution_v<sf::Hw<Hw_t::Vectorizable, int>>,
              "Hw<Scalar,int> and Hw<Vectorizable,int> MUST hash differently — "
              "the tier salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::Hw<Hw_t::Scalar, int>> != dg::row_hash_contribution_v<int>,
              "Hw<Scalar,int> MUST hash differently from bare int — the wrapper "
              "tag (0x2C) discriminates the wrapped value.");

using NvBackend = sf::VendorBackend_v;
static_assert(dg::row_hash_contribution_v<sf::Vendor<NvBackend::NV, sf::Hw<Hw_t::Scalar, int>>>
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
