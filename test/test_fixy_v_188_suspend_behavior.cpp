// FIXY-V-188 sentinel TU: safety/SuspendBehavior.h — the Graded<Absolute,
// SuspendBehaviorLattice::At<Behavior>, T> regime-1 pause-on-suspend witness
// over the V-181 3-element chain (Unknown ⊏ PausesOnSuspend ⊏ KeepsTicking),
// plus safety/IsSuspendBehavior.h (concept extractor) and the
// row_hash_contribution<SuspendBehavior<Behavior, T>> federation-cache
// discriminator wired in safety/diag/RowHashFold.h (salt 0x33 — the row_hash
// V-181 deferred).
//
// THE LOAD-BEARING PROPERTY this TU defends: a KeepsTicking (CLOCK_BOOTTIME)
// witness satisfies a KeepsTicking deadline-watchdog floor; a PausesOnSuspend
// (CLOCK_MONOTONIC) witness does NOT — the static distinction V-194 relies on
// to close the 10-minute-suspend bug class.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - neg_suspend_behavior_pauses_at_watchdog  (satisfies gate: Pauses ⋤ KeepsTicking)
//   - neg_suspend_behavior_cross_assign        (distinct-behavior type mismatch)

#include <crucible/safety/SuspendBehavior.h>
#include <crucible/safety/IsSuspendBehavior.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <string_view>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Sb_t = sf::SuspendBehavior_v;
using Cs_t = sf::ClockSource_v;

using KeepsU64  = sf::SuspendBehavior<Sb_t::KeepsTicking,    unsigned long long>;
using PausesU64 = sf::SuspendBehavior<Sb_t::PausesOnSuspend, unsigned long long>;

// ── Regime-1 sizeof preservation ────────────────────────────────────
static_assert(sizeof(KeepsU64) == sizeof(unsigned long long));
static_assert(sizeof(sf::SuspendBehavior<Sb_t::PausesOnSuspend, int>) == sizeof(int));
static_assert(sizeof(sf::SuspendBehavior<Sb_t::Unknown, char>) == sizeof(char));

// ── satisfies<KeepsTicking> — the V-194 DeadlineWatchdog gate ───────
static_assert( KeepsU64::satisfies<Sb_t::KeepsTicking>);
static_assert(!PausesU64::satisfies<Sb_t::KeepsTicking>,
    "FIXY-V-188: a CLOCK_MONOTONIC witness MUST NOT satisfy a KeepsTicking "
    "requirement — V-194 forces CLOCK_BOOTTIME.");
static_assert( KeepsU64::satisfies<Sb_t::PausesOnSuspend>);
static_assert( PausesU64::satisfies<Sb_t::Unknown>);

// ── Distinct types per behavior ─────────────────────────────────────
static_assert(!std::is_same_v<KeepsU64, PausesU64>);
static_assert(!std::is_convertible_v<PausesU64, KeepsU64>);

// ── IsSuspendBehavior concept extractor ─────────────────────────────
static_assert(ex::IsSuspendBehavior<KeepsU64>);
static_assert(!ex::IsSuspendBehavior<int>);
static_assert(std::is_same_v<ex::suspend_behavior_value_t<KeepsU64>, unsigned long long>);
static_assert(ex::suspend_behavior_v<PausesU64> == Sb_t::PausesOnSuspend);

// ── row_hash distinctness — per-behavior / vs bare ──────────────────
static_assert(dg::row_hash_contribution_v<KeepsU64>
              != dg::row_hash_contribution_v<PausesU64>,
    "KeepsTicking and PausesOnSuspend MUST hash to distinct federation-cache "
    "slots — the behavior salt discriminates.");
static_assert(dg::row_hash_contribution_v<KeepsU64>
              != dg::row_hash_contribution_v<unsigned long long>,
    "a SuspendBehavior witness MUST hash differently from the bare value "
    "(salt 0x33).");

// ── Salt-disjointness from the sister wrapper (ClockSource 0x30) ────
static_assert(dg::row_hash_contribution_v<KeepsU64>
              != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, unsigned long long>>,
    "SuspendBehavior (0x33) and ClockSource (0x30) are distinct wrappers — "
    "their per-wrapper salts MUST discriminate.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) ─────────────────────
static_assert(
    dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, KeepsU64>>
    != dg::row_hash_contribution_v<sf::SuspendBehavior<Sb_t::KeepsTicking, sf::ClockSource<Cs_t::Boot, unsigned long long>>>,
    "ClockSource<Boot, SuspendBehavior<Keeps,u64>> and SuspendBehavior<Keeps, "
    "ClockSource<Boot,u64>> MUST hash differently — row_hash is order-sensitive.");

}  // namespace

int main() {
    ::crucible::safety::detail::suspend_behavior_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_suspend_behavior_smoke_test()) return 1;
    return 0;
}
