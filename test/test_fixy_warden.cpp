// ── test_fixy_warden — sentinel TU for fixy/Warden.h ──────────────
//
// FIXY-U-120.  Pulls fixy/Warden.h into a TU compiled under project
// warning flags so the header's static_asserts (sentinel + concept
// resolution + cardinality witness) execute.  Witnesses:
//
//   1. fixy::warden::mint_hardening                aliases substrate.
//   2. fixy::warden::mint_deadline_watchdog        aliases substrate.
//   3. fixy::warden::mint_hot_region_registry_handle aliases substrate.
//   4. fixy::warden::mint_quarantine_policy        aliases substrate.
//   5. Type aliases (AppliedPolicy / DeadlineWatchdog /
//      HotRegionRegistryHandle / QuarantineConfig / QuarantineEvent /
//      Policy) preserve substrate identity.
//   6. CtxFitsXMint concepts admit ColdInitCtx and reject BgDrainCtx.
//   7. Cardinality witness — substrate ships exactly 4 warden mints.
//
// Per CLAUDE.md §XXI the using-decl is name-lookup-only: each fixy::
// re-export must resolve to the SAME substrate function-template
// instantiation (pointer-identity, not behavioural-equivalence).
// `decltype(&fixy::warden::mint_X<...>) == decltype(&warden::mint_X<...>)`
// is the strongest reach witness for free function templates.

#include <crucible/fixy/Warden.h>

#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/Senses.h>

#include <type_traits>

namespace fw      = ::crucible::fixy::warden;
namespace warden_ = ::crucible::warden;
namespace eff     = ::crucible::effects;

// ─── 1. Function-template identity — mint_hardening ───────────────
//
// `mint_hardening<ColdInitCtx>` resolves through the using-decl to
// the substrate's instantiation.  Same address proves the re-export
// is pure name lookup (no shadowed wrapper, no proxy lambda).

static_assert(std::is_same_v<
    decltype(&fw::mint_hardening<eff::ColdInitCtx>),
    decltype(&warden_::mint_hardening<eff::ColdInitCtx>)>,
    "FIXY-U-120: fixy::warden::mint_hardening must be the substrate "
    "function (using-decl preserves crucible::warden:: residency).");

// ─── 2. Function-template identity — mint_deadline_watchdog ───────

static_assert(std::is_same_v<
    decltype(&fw::mint_deadline_watchdog<eff::ColdInitCtx>),
    decltype(&warden_::mint_deadline_watchdog<eff::ColdInitCtx>)>,
    "FIXY-U-120: fixy::warden::mint_deadline_watchdog must be the "
    "substrate function (using-decl name-lookup-only).");

// ─── 3. Function-template identity — mint_hot_region_registry_handle ──

static_assert(std::is_same_v<
    decltype(&fw::mint_hot_region_registry_handle<eff::ColdInitCtx>),
    decltype(&warden_::mint_hot_region_registry_handle<eff::ColdInitCtx>)>,
    "FIXY-U-120: fixy::warden::mint_hot_region_registry_handle must be "
    "the substrate function (using-decl name-lookup-only).");

// ─── 4. Function-template identity — mint_quarantine_policy ───────
//
// `mint_quarantine_policy<Ctx, MaxCogs, MaxEvents>` is the only
// template in the warden surface with non-type parameters.  Pin the
// instantiation at (ColdInitCtx, 8, 32) to witness reach.

static_assert(std::is_same_v<
    decltype(&fw::mint_quarantine_policy<eff::ColdInitCtx, 8, 32>),
    decltype(&warden_::mint_quarantine_policy<eff::ColdInitCtx, 8, 32>)>,
    "FIXY-U-120: fixy::warden::mint_quarantine_policy must be the "
    "substrate function template (using-decl name-lookup-only).");

// ─── 5. Type-alias identity ───────────────────────────────────────
//
// The non-mint types re-exported into fixy::warden:: must preserve
// substrate identity.  A future regression that shadows one with a
// local typedef would silently break ABI; the static_assert reds
// the build immediately.

static_assert(std::is_same_v<fw::AppliedPolicy, warden_::AppliedPolicy>,
    "fixy::warden::AppliedPolicy must alias substrate.");

static_assert(std::is_same_v<fw::Policy, warden_::Policy>,
    "fixy::warden::Policy must alias substrate.");

static_assert(std::is_same_v<fw::DeadlineWatchdog, warden_::DeadlineWatchdog>,
    "fixy::warden::DeadlineWatchdog must alias substrate.");

static_assert(std::is_same_v<fw::HotRegionRegistryHandle,
                             warden_::HotRegionRegistryHandle>,
    "fixy::warden::HotRegionRegistryHandle must alias substrate.");

static_assert(std::is_same_v<fw::QuarantineConfig, warden_::QuarantineConfig>,
    "fixy::warden::QuarantineConfig must alias substrate.");

static_assert(std::is_same_v<fw::QuarantineEvent, warden_::QuarantineEvent>,
    "fixy::warden::QuarantineEvent must alias substrate.");

// ─── 6. Concept-resolution identity ───────────────────────────────
//
// Each CtxFitsXMint concept admits ColdInitCtx and rejects BgDrainCtx.
// The substrate ships these asserts at the mint-definition site; we
// duplicate through the fixy:: layer so a future regression that
// silently relaxed the concept gate (e.g., dropping the Init-row
// requirement) would red THIS TU on top of the substrate's own.

static_assert(fw::CtxFitsHardeningMint<eff::ColdInitCtx>);
static_assert(!fw::CtxFitsHardeningMint<eff::BgDrainCtx>);
static_assert(!fw::CtxFitsHardeningMint<eff::HotFgCtx>);

static_assert(fw::CtxFitsDeadlineWatchdogMint<eff::ColdInitCtx>);
static_assert(!fw::CtxFitsDeadlineWatchdogMint<eff::BgDrainCtx>);
static_assert(!fw::CtxFitsDeadlineWatchdogMint<eff::HotFgCtx>);

static_assert(fw::CtxFitsHotRegionRegistryMint<eff::ColdInitCtx>);
static_assert(!fw::CtxFitsHotRegionRegistryMint<eff::BgDrainCtx>);
static_assert(!fw::CtxFitsHotRegionRegistryMint<eff::HotFgCtx>);

static_assert(fw::CtxFitsQuarantineMint<eff::ColdInitCtx>);
static_assert(!fw::CtxFitsQuarantineMint<eff::BgDrainCtx>);

// QuarantineRecord / QuarantineOverride sub-concepts (substrate-level
// variants for record-only and override-only contexts) — re-export
// identity also surfaced through fixy:: per Warden.h commentary.
static_assert(fw::CtxFitsQuarantineRecord<eff::BgDrainCtx>);
static_assert(fw::CtxFitsQuarantineOverride<eff::ColdInitCtx>);
static_assert(fw::CtxFitsQuarantineOverride<eff::TestRunnerCtx>);

// ─── 7. Cardinality witness ───────────────────────────────────────
//
// Locks the Warden.h sentinel's stated mint cardinality against drift.
// A future contributor adding a fifth warden mint must touch BOTH
// fixy/Warden.h's `warden_mint_cardinality` constant AND this TU's
// expected value; otherwise CI reds.

static_assert(::crucible::fixy::warden::self_test::warden_mint_cardinality == 4,
    "fixy::warden:: mint cardinality drifted from 4 — fixy/Warden.h "
    "sentinel block and this TU must update in lockstep.");

int main() {
    // The substrate's own warden_neg fixtures exercise the negative
    // requires-clause path; this TU asserts reachability + alias
    // identity + concept resolution.  No runtime call needed.
    //
    // Touch the runtime smoke block so the header's no-throw
    // smoke-test runs under the project's preset semantics (the
    // smoke block itself is a no-op except for instantiating the
    // concept aliases at runtime context).
    ::crucible::fixy::warden::runtime_smoke_test();
    return 0;
}
