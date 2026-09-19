// A barrier tier is a floor, not a ceiling.  A value meets a consumer that
// asks for some minimum ordering when its own published fence is at or
// above that minimum, and weakening moves down the chain.
//
// The checks here are the ones the wrapper header cannot make about
// itself: its place in the dimension table, the distinctness of its hash
// contribution, and how it composes with other wrappers.

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/IsBarrierGuarded.h>
#include <crucible/safety/Hw.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Bs_t = sf::BarrierStrength_v;

static_assert(sizeof(sf::BarrierGuarded<Bs_t::None, int>) == sizeof(int));
static_assert(sizeof(sf::BarrierGuarded<Bs_t::AcqRel, double>) == sizeof(double));
static_assert(sizeof(sf::BarrierGuarded<Bs_t::FullFence, char>) == sizeof(char));

static_assert(sf::BarrierGuarded<Bs_t::SeqCst, int>::satisfies<Bs_t::AcqRel>);
static_assert(!sf::BarrierGuarded<Bs_t::AcquireLoad, int>::satisfies<Bs_t::AcqRel>);

static_assert(sf::DimensionedGradedWrapper<sf::BarrierGuarded<Bs_t::AcqRel, int>>);
static_assert(sf::wrapper_dimension_v<sf::BarrierGuarded<Bs_t::AcqRel, int>> == sf::DimensionAxis::BarrierStrength);
static_assert(sf::wrapper_tier_v<sf::BarrierGuarded<Bs_t::AcqRel, int>> == sf::TierKind::Semiring);
static_assert(sf::wrapper_modality_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::verify_quadruple<sf::BarrierGuarded<Bs_t::SeqCst, int>>(),
              "BarrierGuarded<> must satisfy the (lattice, modality, tier, axis) "
              "quadruple.");

static_assert(ex::IsBarrierGuarded<sf::BarrierGuarded<Bs_t::AcqRel, int>>);
static_assert(!ex::IsBarrierGuarded<int>);
static_assert(std::is_same_v<ex::barrier_guarded_value_t<sf::BarrierGuarded<Bs_t::SeqCst, double>>, double>);
static_assert(ex::barrier_guarded_tier_v<sf::BarrierGuarded<Bs_t::FullFence, int>> == Bs_t::FullFence);

static_assert(dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
                  != dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::SeqCst, int>>,
              "two barrier tiers over the same payload must hash differently, or "
              "they share one federation-cache slot");
static_assert(dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, int>> != dg::row_hash_contribution_v<int>,
              "a guarded value must hash differently from the bare value it wraps");

// The two enums below meet at the same ordinal on purpose.  Two wrappers
// that happen to agree numerically still belong to different axes, and the
// assertion after this one is what shows the hash keeps them apart.
using HwInstr_t = sf::HwInstruction_v;
static_assert(static_cast<std::uint8_t>(Bs_t::AcqRel) == static_cast<std::uint8_t>(HwInstr_t::PrivilegedMsr),
              "the premise of the next assertion is that both enumerators sit at the "
              "same ordinal");
static_assert(dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, int>>
                  != dg::row_hash_contribution_v<sf::Hw<HwInstr_t::PrivilegedMsr, int>>,
              "two wrappers at the same enum ordinal must still hash differently, "
              "because they name different axes");

static_assert(dg::row_hash_contribution_v<sf::Hw<HwInstr_t::Scalar, sf::BarrierGuarded<Bs_t::AcqRel, int>>>
                  != dg::row_hash_contribution_v<sf::BarrierGuarded<Bs_t::AcqRel, sf::Hw<HwInstr_t::Scalar, int>>>,
              "the same two wrappers nested in opposite orders must hash "
              "differently, because the fold is order-sensitive");

// A guarded value can be pinned to a provenance as well.  Which wrapper
// goes outside changes the meaning, so it must change the cache slot too.
using AcqRelInt = sf::BarrierGuarded<Bs_t::AcqRel, int>;
using TaggedOuter = sf::Tagged<AcqRelInt, sf::source::FromInternal>;
using TaggedInner = sf::BarrierGuarded<Bs_t::AcqRel, sf::Tagged<int, sf::source::FromInternal>>;
static_assert(dg::row_hash_contribution_v<TaggedOuter> != dg::row_hash_contribution_v<TaggedInner>,
              "tagging a guarded value and guarding a tagged value must hash "
              "differently; the composition is order-sensitive");
static_assert(dg::row_hash_contribution_v<TaggedOuter> != dg::row_hash_contribution_v<AcqRelInt>,
              "adding a provenance tag must change the cache slot, because the "
              "tagged and untagged values are distinct compositions");

}  // namespace

int main() {
    ::crucible::safety::detail::barrier_guarded_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_barrier_guarded_smoke_test()) return 1;
    return 0;
}
