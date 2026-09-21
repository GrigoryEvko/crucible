// The wrapper names which physical clock a value came from.  There is no
// operation to relax that: a clock source cannot be soundly re-labelled
// once read.  It projects the determinism, suspend and pinning behaviour of
// its source, which is what a deadline watchdog gates on.
//
// It sits on no dimension axis of its own, so there is no dimension
// quadruple to assert here.  The checks are the ones the wrapper header
// cannot make about itself: distinctness of hash contributions and
// sensitivity to nesting order.

#include <crucible/safety/_ClockSource.h>
#include <crucible/safety/IsClockSource.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Cs_t = sf::ClockSource_v;
using Ms_t = sf::MemoryScope_v;
using Det = sf::DetSafeTier_v;
using Sus = sf::SuspendBehavior_v;
using Pin = sf::PinningRequirement_v;

template <typename T>
using Cs = sf::ClockSource<Cs_t::Boot, T>;

static_assert(sizeof(sf::BootClockBytes<int>) == sizeof(int));
static_assert(sizeof(sf::MonotonicClockBytes<double>) == sizeof(double));
static_assert(sizeof(sf::TscBytes<unsigned long long>) == sizeof(unsigned long long));
static_assert(sizeof(sf::RealtimeClockBytes<char>) == sizeof(char));

static_assert(sf::BootClockBytes<int>::suspend_behavior == Sus::KeepsTicking);
static_assert(sf::MonotonicClockBytes<int>::suspend_behavior == Sus::PausesOnSuspend);
static_assert(sf::RealtimeClockBytes<int>::det_safe_tier == Det::WallClockRead);
static_assert(sf::TscBytes<int>::pinning_requirement == Pin::PerCore);
// Reading a clock is never pure, whichever clock it is.
static_assert(sf::BootClockBytes<int>::det_safe_tier != Det::Pure);
static_assert(sf::TscBytes<int>::det_safe_tier != Det::Pure);

static_assert(sf::BootClockBytes<int>::satisfies<Cs_t::Boot>);
static_assert(!sf::MonotonicClockBytes<int>::satisfies<Cs_t::Boot>,
              "a monotonic clock must not satisfy a boot-clock requirement, because "
              "it pauses while the machine is suspended");
static_assert(sf::TscBytes<int>::satisfies<Cs_t::Boot>);

static_assert(!std::is_same_v<sf::BootClockBytes<int>, sf::MonotonicClockBytes<int>>);
static_assert(!std::is_convertible_v<sf::MonotonicClockBytes<int>, sf::BootClockBytes<int>>);

static_assert(ex::IsClockSource<sf::BootClockBytes<int>>);
static_assert(!ex::IsClockSource<int>);
static_assert(std::is_same_v<ex::clock_source_value_t<sf::TscBytes<double>>, double>);
static_assert(ex::clock_source_source_v<sf::MonotonicClockBytes<int>> == Cs_t::Monotonic);

static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<int>>
                  != dg::row_hash_contribution_v<sf::MonotonicClockBytes<int>>,
              "two clock sources over the same payload must hash differently, or "
              "they share one federation-cache slot");
static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<int>> != dg::row_hash_contribution_v<int>,
              "a clock-sourced value must hash differently from the bare value it "
              "wraps");

// These two project to the same determinism, suspend and pinning triple,
// so nothing but the source itself separates them.
static_assert(dg::row_hash_contribution_v<sf::TscBytes<int>> != dg::row_hash_contribution_v<sf::PmuBytes<int>>,
              "two sources that project to the same triple are still distinct "
              "sources and must occupy distinct slots");

// The same collapse, on the pair where it matters most: merging these two
// would put hardware-timestamp reads and boot-clock reads in one slot
// across the whole fleet.
static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<unsigned long long>>
                  != dg::row_hash_contribution_v<sf::PtpHwClockBytes<unsigned long long>>,
              "the boot clock and the hardware clock project to the same triple and "
              "must still occupy distinct slots; the source is the only thing "
              "separating them");

static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<int>>
                  != dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, int>>,
              "two different wrappers over the same payload must hash differently");

static_assert(dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, sf::BootClockBytes<int>>>
                  != dg::row_hash_contribution_v<sf::BootClockBytes<sf::ScopedFence<Ms_t::Cta, int>>>,
              "the same two wrappers nested in opposite orders must hash "
              "differently, because the fold is order-sensitive");

}  // namespace

int main() {
    ::crucible::safety::detail::clock_source_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_clock_source_smoke_test()) return 1;
    return 0;
}
