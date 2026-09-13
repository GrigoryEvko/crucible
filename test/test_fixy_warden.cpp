// A re-export must be name lookup and nothing else: the re-exported name
// has to resolve to the same substrate instantiation, not merely to
// something that behaves the same way.  Comparing the types of the two
// function pointers is the strongest available witness of that for a free
// function template, which is why the assertions below take addresses.

#include <crucible/fixy/Warden.h>

#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/Senses.h>

#include <type_traits>

namespace fw = ::crucible::fixy::warden;
namespace warden_ = ::crucible::warden;
namespace eff = ::crucible::effects;

static_assert(std::is_same_v<decltype(&fw::mint_hardening<eff::ColdInitCtx>),
                             decltype(&warden_::mint_hardening<eff::ColdInitCtx>)>,
              "fixy::warden::mint_hardening must be the substrate function");

static_assert(std::is_same_v<decltype(&fw::mint_deadline_watchdog<eff::ColdInitCtx>),
                             decltype(&warden_::mint_deadline_watchdog<eff::ColdInitCtx>)>,
              "fixy::warden::mint_deadline_watchdog must be the substrate function");

static_assert(std::is_same_v<decltype(&fw::mint_hot_region_registry_handle<eff::ColdInitCtx>),
                             decltype(&warden_::mint_hot_region_registry_handle<eff::ColdInitCtx>)>,
              "fixy::warden::mint_hot_region_registry_handle must be the substrate "
              "function");

// The only warden mint with non-type parameters.  Any pair of values pins
// the reach, and these two are arbitrary.
static_assert(std::is_same_v<decltype(&fw::mint_quarantine_policy<eff::ColdInitCtx, 8, 32>),
                             decltype(&warden_::mint_quarantine_policy<eff::ColdInitCtx, 8, 32>)>,
              "fixy::warden::mint_quarantine_policy must be the substrate function "
              "template");

// A shadowing local typedef of the same shape would change the type
// identity while leaving every use site compiling.  These assertions are
// what makes that fail.

static_assert(std::is_same_v<fw::AppliedPolicy, warden_::AppliedPolicy>,
              "fixy::warden::AppliedPolicy must alias substrate.");

static_assert(std::is_same_v<fw::Policy, warden_::Policy>, "fixy::warden::Policy must alias substrate.");

static_assert(std::is_same_v<fw::DeadlineWatchdog, warden_::DeadlineWatchdog>,
              "fixy::warden::DeadlineWatchdog must alias substrate.");

static_assert(std::is_same_v<fw::HotRegionRegistryHandle, warden_::HotRegionRegistryHandle>,
              "fixy::warden::HotRegionRegistryHandle must alias substrate.");

static_assert(std::is_same_v<fw::QuarantineConfig, warden_::QuarantineConfig>,
              "fixy::warden::QuarantineConfig must alias substrate.");

static_assert(std::is_same_v<fw::QuarantineEvent, warden_::QuarantineEvent>,
              "fixy::warden::QuarantineEvent must alias substrate.");

// QuarantineSnapshot is trivially copyable, so a hand-rolled substitute of
// the same shape would satisfy every other use in this file.
static_assert(std::is_same_v<fw::QuarantineTransition, warden_::QuarantineTransition>,
              "fixy::warden::QuarantineTransition must alias substrate.");

static_assert(std::is_same_v<fw::QuarantineSnapshot, warden_::QuarantineSnapshot>,
              "fixy::warden::QuarantineSnapshot must alias substrate.");

// The substrate asserts the same gates at the definition site.  Restating
// them through the re-export catches a relaxed gate here as well, which is
// the layer a caller actually names.

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

static_assert(fw::CtxFitsQuarantineRecord<eff::BgDrainCtx>);
static_assert(fw::CtxFitsQuarantineOverride<eff::ColdInitCtx>);
static_assert(fw::CtxFitsQuarantineOverride<eff::TestRunnerCtx>);

// A floor, not an exact count.  The exact pin sits next to the constant it
// counts, where a contributor raising it cannot miss the sibling assertion.
// Here only the other direction matters: a mint removed without review.

static_assert(::crucible::fixy::warden::self_test::warden_mint_cardinality >= 4,
              "floor: warden mint cardinality regressed below 4, so a warden mint "
              "was removed without updating the colocated exact pin");

int main() {
    ::crucible::fixy::warden::runtime_smoke_test();
    return 0;
}
