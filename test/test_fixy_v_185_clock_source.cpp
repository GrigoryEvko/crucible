// FIXY-V-185 sentinel TU: safety/ClockSource.h — the Graded<Absolute,
// ClockSourceLattice::At<Source>, T> regime-1 carrier for the V-184
// ClockSource axis, plus safety/IsClockSource.h (concept extractor) and
// the row_hash_contribution<safety::ClockSource<Source, Inner>>
// federation-cache discriminator wired in safety/diag/RowHashFold.h
// (salt 0x30 — the row_hash V-184 deferred).
//
// This TU forces every header-embedded static_assert to compile under the
// project warning flags (header-only static_asserts are otherwise
// unverified — feedback_header_only_static_assert_blind_spot) AND adds the
// cross-cutting checks the wrapper header cannot self-contain: row_hash
// distinctness + nesting-order sensitivity, salt-disjointness from the
// sister Repr-neighborhood wrapper (ScopedFence, 0x2F), and the runtime
// smoke tests.
//
// ClockSource is the PROVENANCE sibling of ScopedFence: it has NO `relax`
// (a physical clock source cannot be soundly re-labelled), and exposes the
// projected (DetSafe, Suspend, Pinning) axes so V-194's DeadlineWatchdog
// gates on KeepsTicking.  ClockSource carries NO DimensionAxis enumerator
// (V-184 shipped none) — it is an off-tree Repr-neighborhood wrapper, so
// there is no DimensionTraits quadruple to assert.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - test/safety_neg/neg_clock_source_cross_source_assign.cpp          (distinct-source type mismatch)
//   - test/safety_neg/neg_clock_source_suspend_gate_rejects_monotonic.cpp (axis-requirement gate)

#include <crucible/safety/ClockSource.h>
#include <crucible/safety/IsClockSource.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using Cs_t = sf::ClockSource_v;
using Ms_t = sf::MemoryScope_v;
using Det  = sf::DetSafeTier_v;
using Sus  = sf::SuspendBehavior_v;
using Pin  = sf::PinningRequirement_v;

template <typename T> using Cs = sf::ClockSource<Cs_t::Boot, T>;  // shorthand witness

// ── Regime-1 sizeof preservation — distinct sources, same payload ──
static_assert(sizeof(sf::BootClockBytes<int>)                == sizeof(int));
static_assert(sizeof(sf::MonotonicClockBytes<double>)        == sizeof(double));
static_assert(sizeof(sf::TscBytes<unsigned long long>)       == sizeof(unsigned long long));
static_assert(sizeof(sf::RealtimeClockBytes<char>)           == sizeof(char));

// ── Projected (DetSafe, Suspend, Pinning) axes — the V-184 points ──
static_assert(sf::BootClockBytes<int>::suspend_behavior      == Sus::KeepsTicking);
static_assert(sf::MonotonicClockBytes<int>::suspend_behavior == Sus::PausesOnSuspend);
static_assert(sf::RealtimeClockBytes<int>::det_safe_tier     == Det::WallClockRead);
static_assert(sf::TscBytes<int>::pinning_requirement         == Pin::PerCore);
// No clock source is DetSafe::Pure (a clock read is never pure).
static_assert(sf::BootClockBytes<int>::det_safe_tier         != Det::Pure);
static_assert(sf::TscBytes<int>::det_safe_tier               != Det::Pure);

// ── satisfies<Required> — the V-194 DeadlineWatchdog gate ───────────
static_assert( sf::BootClockBytes<int>::satisfies<Cs_t::Boot>);
static_assert(!sf::MonotonicClockBytes<int>::satisfies<Cs_t::Boot>,
    "MonotonicClockBytes MUST NOT satisfy a Boot requirement — it pauses on "
    "suspend; this is the static distinction V-194 relies on.");
static_assert( sf::TscBytes<int>::satisfies<Cs_t::Boot>);

// ── Distinct types per source — the V-194 static-distinction basis ──
static_assert(!std::is_same_v<sf::BootClockBytes<int>, sf::MonotonicClockBytes<int>>);
static_assert(!std::is_convertible_v<sf::MonotonicClockBytes<int>, sf::BootClockBytes<int>>);

// ── IsClockSource concept extractor ─────────────────────────────────
static_assert(ex::IsClockSource<sf::BootClockBytes<int>>);
static_assert(!ex::IsClockSource<int>);
static_assert(std::is_same_v<ex::clock_source_value_t<sf::TscBytes<double>>, double>);
static_assert(ex::clock_source_source_v<sf::MonotonicClockBytes<int>> == Cs_t::Monotonic);

// ── row_hash distinctness — different sources, different payloads ───
static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<int>>
              != dg::row_hash_contribution_v<sf::MonotonicClockBytes<int>>,
    "BootClockBytes<int> and MonotonicClockBytes<int> MUST hash differently "
    "— the source salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<int>>
              != dg::row_hash_contribution_v<int>,
    "BootClockBytes<int> MUST hash differently from bare int — the wrapper "
    "tag (0x30) discriminates the wrapped value.");
static_assert(dg::row_hash_contribution_v<sf::TscBytes<int>>
              != dg::row_hash_contribution_v<sf::PmuBytes<int>>,
    "TscRaw and PmuCounter project to the same (DetSafe,Suspend,Pin) tuple "
    "but are DISTINCT sources — they MUST occupy distinct row_hash slots.");

// ── Salt-disjointness from the sister Repr wrapper (ScopedFence 0x2F) ─
static_assert(dg::row_hash_contribution_v<sf::BootClockBytes<int>>
              != dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, int>>,
    "ClockSource (0x30) and ScopedFence (0x2F) are distinct Repr-"
    "neighborhood wrappers — their per-wrapper salts MUST discriminate.");

// ── Nesting-order sensitivity (§XVI / GAPS-029) ─────────────────────
static_assert(
    dg::row_hash_contribution_v<sf::ScopedFence<Ms_t::Cta, sf::BootClockBytes<int>>>
    != dg::row_hash_contribution_v<sf::BootClockBytes<sf::ScopedFence<Ms_t::Cta, int>>>,
    "ScopedFence<Cta, BootClockBytes<int>> and BootClockBytes<ScopedFence<"
    "Cta,int>> MUST hash differently — row_hash is order-sensitive per the "
    "canonical wrapper-nesting discipline.");

}  // namespace

int main() {
    ::crucible::safety::detail::clock_source_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_clock_source_smoke_test()) return 1;
    return 0;
}
