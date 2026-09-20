// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and adds the cross-wrapper checks no
// single wrapper header can state about itself.

#include <crucible/safety/CpuPinned.h>
#include <crucible/safety/IsCpuPinned.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/SchedClass.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using AffinityMask = ::crucible::algebra::lattices::AffinityMask;
using Posture = sf::PinningPosture;
using Cs_t = sf::ClockSource_v;
using Sp_t = sf::SchedulerPolicy_v;

inline constexpr AffinityMask kC0 = AffinityMask::single(0);
inline constexpr AffinityMask kC5 = AffinityMask::single(5);
inline constexpr AffinityMask k2 = AffinityMask::range(0, 1);

using PinnedC0 = sf::CpuPinned<kC0, Posture::PinnedExplicit, int>;
using AutoC0 = sf::CpuPinned<kC0, Posture::PinnedAuto, int>;
using TwoBit = sf::CpuPinned<k2, Posture::PinnedExplicit, int>;

static_assert(sizeof(PinnedC0) == sizeof(int));
static_assert(sizeof(sf::CpuPinned<kC5, Posture::PinnedExplicit, unsigned long long>) == sizeof(unsigned long long));

static_assert(!std::is_copy_constructible_v<PinnedC0>);
static_assert(!std::is_copy_assignable_v<PinnedC0>);
static_assert(std::is_move_constructible_v<PinnedC0>);

static_assert(PinnedC0::is_singleton_pin);
static_assert(!TwoBit::is_singleton_pin, "a 2-core mask is not a singleton, and the TSC-reader gate rejects it.");
static_assert(PinnedC0::meets_posture<Posture::PinnedExplicit>);
static_assert(!AutoC0::meets_posture<Posture::PinnedExplicit>, "PinnedAuto does not meet a PinnedExplicit floor.");

static_assert(ex::IsCpuPinned<PinnedC0>);
static_assert(!ex::IsCpuPinned<int>);
static_assert(std::is_same_v<ex::cpu_pinned_value_t<PinnedC0>, int>);
static_assert(ex::cpu_pinned_posture_v<AutoC0> == Posture::PinnedAuto);

static_assert(dg::row_hash_contribution_v<PinnedC0> != dg::row_hash_contribution_v<AutoC0>,
              "different postures MUST hash to distinct federation-cache slots.");
static_assert(dg::row_hash_contribution_v<PinnedC0>
                  != dg::row_hash_contribution_v<sf::CpuPinned<kC5, Posture::PinnedExplicit, int>>,
              "different pinned cores MUST hash to distinct slots.");
static_assert(dg::row_hash_contribution_v<PinnedC0> != dg::row_hash_contribution_v<int>,
              "a CpuPinned proof MUST hash differently from the bare wrapped value (salt 0x32).");

static_assert(dg::row_hash_contribution_v<PinnedC0> != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, int>>);
static_assert(dg::row_hash_contribution_v<PinnedC0> != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>);

static_assert(
    dg::row_hash_contribution_v<sf::CpuPinned<kC0, Posture::PinnedExplicit, sf::ClockSource<Cs_t::Boot, int>>>
        != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, sf::CpuPinned<kC0, Posture::PinnedExplicit, int>>>,
    "CpuPinned<…, ClockSource<Boot,int>> and ClockSource<Boot, CpuPinned<…,int>> "
    "MUST hash differently — row_hash is order-sensitive.");

}  // namespace

int main() {
    ::crucible::safety::detail::cpu_pinned_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_cpu_pinned_smoke_test()) return 1;
    return 0;
}
