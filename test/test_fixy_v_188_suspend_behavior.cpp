// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and adds the cross-wrapper checks no
// single wrapper header can state about itself.

#include <crucible/safety/SuspendBehavior.h>
#include <crucible/safety/IsSuspendBehavior.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <string_view>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Sb_t = sf::SuspendBehavior_v;
using Cs_t = sf::ClockSource_v;

using KeepsU64 = sf::SuspendBehavior<Sb_t::KeepsTicking, unsigned long long>;
using PausesU64 = sf::SuspendBehavior<Sb_t::PausesOnSuspend, unsigned long long>;

static_assert(sizeof(KeepsU64) == sizeof(unsigned long long));
static_assert(sizeof(sf::SuspendBehavior<Sb_t::PausesOnSuspend, int>) == sizeof(int));
static_assert(sizeof(sf::SuspendBehavior<Sb_t::Unknown, char>) == sizeof(char));

static_assert(KeepsU64::satisfies<Sb_t::KeepsTicking>);
static_assert(!PausesU64::satisfies<Sb_t::KeepsTicking>, "a CLOCK_MONOTONIC witness must not satisfy a KeepsTicking "
                                                         "requirement. A watchdog that has to survive suspend needs "
                                                         "CLOCK_BOOTTIME.");
static_assert(KeepsU64::satisfies<Sb_t::PausesOnSuspend>);
static_assert(PausesU64::satisfies<Sb_t::Unknown>);

static_assert(!std::is_same_v<KeepsU64, PausesU64>);
static_assert(!std::is_convertible_v<PausesU64, KeepsU64>);

static_assert(ex::IsSuspendBehavior<KeepsU64>);
static_assert(!ex::IsSuspendBehavior<int>);
static_assert(std::is_same_v<ex::suspend_behavior_value_t<KeepsU64>, unsigned long long>);
static_assert(ex::suspend_behavior_v<PausesU64> == Sb_t::PausesOnSuspend);

static_assert(dg::row_hash_contribution_v<KeepsU64> != dg::row_hash_contribution_v<PausesU64>,
              "KeepsTicking and PausesOnSuspend MUST hash to distinct federation-cache "
              "slots — the behavior salt discriminates.");
static_assert(dg::row_hash_contribution_v<KeepsU64> != dg::row_hash_contribution_v<unsigned long long>,
              "a SuspendBehavior witness MUST hash differently from the bare value "
              "(salt 0x33).");

static_assert(dg::row_hash_contribution_v<KeepsU64>
                  != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, unsigned long long>>,
              "SuspendBehavior (0x33) and ClockSource (0x30) are distinct wrappers — "
              "their per-wrapper salts MUST discriminate.");

static_assert(dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, KeepsU64>>
                  != dg::row_hash_contribution_v<
                      sf::SuspendBehavior<Sb_t::KeepsTicking, sf::ClockSource<Cs_t::Boot, unsigned long long>>>,
              "ClockSource<Boot, SuspendBehavior<Keeps,u64>> and SuspendBehavior<Keeps, "
              "ClockSource<Boot,u64>> MUST hash differently — row_hash is order-sensitive.");

}  // namespace

int main() {
    ::crucible::safety::detail::suspend_behavior_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_suspend_behavior_smoke_test()) return 1;
    return 0;
}
