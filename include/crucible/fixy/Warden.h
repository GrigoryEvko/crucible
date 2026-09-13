#pragma once

// Every warden mint changes process-wide state at startup, so each one
// admits only a context that carries the Init effect.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Hardening.h>
#include <crucible/warden/Quarantine.h>
#include <crucible/warden/Registry.h>

#include <type_traits>

namespace crucible::fixy::warden {

using ::crucible::warden::mint_hardening;
using ::crucible::warden::CtxFitsHardeningMint;
using ::crucible::warden::AppliedPolicy;
using ::crucible::warden::Policy;
using ::crucible::warden::Hardening;

using ::crucible::warden::mint_deadline_watchdog;
using ::crucible::warden::CtxFitsDeadlineWatchdogMint;
using ::crucible::warden::DeadlineWatchdog;

using ::crucible::warden::mint_hot_region_registry_handle;
using ::crucible::warden::CtxFitsHotRegionRegistryMint;
using ::crucible::warden::HotRegionRegistryHandle;

using ::crucible::warden::mint_quarantine_policy;
using ::crucible::warden::CtxFitsQuarantineMint;
using ::crucible::warden::CtxFitsQuarantineRecord;
using ::crucible::warden::CtxFitsQuarantineOverride;
using ::crucible::warden::QuarantinePolicy;
using ::crucible::warden::QuarantineConfig;
using ::crucible::warden::QuarantineTransition;
using ::crucible::warden::QuarantineSnapshot;
using ::crucible::warden::QuarantineEvent;

}  // namespace crucible::fixy::warden

// These assertions repeat what the substrate already asserts at each mint
// definition. The duplication is deliberate. A shadowed local declaration on
// this layer reddens at every consumer's include rather than only in a test.

namespace crucible::fixy::warden::self_test {

static_assert(std::is_same_v<::crucible::fixy::warden::AppliedPolicy, ::crucible::warden::AppliedPolicy>,
              "fixy::warden::AppliedPolicy must alias warden::AppliedPolicy.");

static_assert(std::is_same_v<::crucible::fixy::warden::Policy, ::crucible::warden::Policy>,
              "fixy::warden::Policy must alias warden::Policy.");

static_assert(std::is_same_v<::crucible::fixy::warden::DeadlineWatchdog, ::crucible::warden::DeadlineWatchdog>,
              "fixy::warden::DeadlineWatchdog must alias warden::DeadlineWatchdog.");

static_assert(
    std::is_same_v<::crucible::fixy::warden::HotRegionRegistryHandle, ::crucible::warden::HotRegionRegistryHandle>,
    "fixy::warden::HotRegionRegistryHandle must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::warden::QuarantineConfig, ::crucible::warden::QuarantineConfig>,
              "fixy::warden::QuarantineConfig must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::warden::QuarantineEvent, ::crucible::warden::QuarantineEvent>,
              "fixy::warden::QuarantineEvent must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::warden::QuarantineTransition, ::crucible::warden::QuarantineTransition>,
              "fixy::warden::QuarantineTransition must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::warden::QuarantineSnapshot, ::crucible::warden::QuarantineSnapshot>,
              "fixy::warden::QuarantineSnapshot must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::warden::Hardening, ::crucible::warden::Hardening>,
              "fixy::warden::Hardening must alias substrate.");

static_assert(::crucible::fixy::warden::CtxFitsHardeningMint<::crucible::effects::ColdInitCtx>,
              "fixy::warden::CtxFitsHardeningMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::warden::CtxFitsDeadlineWatchdogMint<::crucible::effects::ColdInitCtx>,
              "fixy::warden::CtxFitsDeadlineWatchdogMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::warden::CtxFitsHotRegionRegistryMint<::crucible::effects::ColdInitCtx>,
              "fixy::warden::CtxFitsHotRegionRegistryMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::warden::CtxFitsQuarantineMint<::crucible::effects::ColdInitCtx>,
              "fixy::warden::CtxFitsQuarantineMint must admit ColdInitCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsHardeningMint<::crucible::effects::BgDrainCtx>,
              "fixy::warden::CtxFitsHardeningMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsDeadlineWatchdogMint<::crucible::effects::BgDrainCtx>,
              "fixy::warden::CtxFitsDeadlineWatchdogMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsHotRegionRegistryMint<::crucible::effects::BgDrainCtx>,
              "fixy::warden::CtxFitsHotRegionRegistryMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsQuarantineMint<::crucible::effects::BgDrainCtx>,
              "fixy::warden::CtxFitsQuarantineMint must reject BgDrainCtx.");

// The two rejection classes differ. BgDrainCtx carries a row without Init.
// HotFgCtx carries an empty row. Quarantine is absent from this trio because
// the substrate ships no HotFgCtx rejection for it.
static_assert(!::crucible::fixy::warden::CtxFitsHardeningMint<::crucible::effects::HotFgCtx>,
              "fixy::warden::CtxFitsHardeningMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsDeadlineWatchdogMint<::crucible::effects::HotFgCtx>,
              "fixy::warden::CtxFitsDeadlineWatchdogMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::warden::CtxFitsHotRegionRegistryMint<::crucible::effects::HotFgCtx>,
              "fixy::warden::CtxFitsHotRegionRegistryMint must reject HotFgCtx.");

// These two concepts query the caller for a non-mint quarantine operation
// rather than gate the mint, so they admit contexts the mint itself rejects.
// A drain context records a transition without minting. An init or test
// context force-clears a quarantine state.
static_assert(::crucible::fixy::warden::CtxFitsQuarantineRecord<::crucible::effects::BgDrainCtx>,
              "fixy::warden::CtxFitsQuarantineRecord must admit BgDrainCtx "
              "(the steady-state drain context records transitions).");

static_assert(::crucible::fixy::warden::CtxFitsQuarantineOverride<::crucible::effects::ColdInitCtx>,
              "fixy::warden::CtxFitsQuarantineOverride must admit ColdInitCtx "
              "(Keeper init-time override authority).");

static_assert(::crucible::fixy::warden::CtxFitsQuarantineOverride<::crucible::effects::TestRunnerCtx>,
              "fixy::warden::CtxFitsQuarantineOverride must admit TestRunnerCtx "
              "(deterministic test-runner override authority).");

// The exact ceiling pin sits next to the re-export list so a contributor
// changing the count cannot miss it. The sibling test holds only a floor pin,
// which catches an accidental removal instead.
inline constexpr int warden_mint_cardinality = 4;

static_assert(warden_mint_cardinality == 4, "fixy::warden:: re-exports exactly 4 mint factories — "
                                            "mint_hardening, mint_deadline_watchdog, "
                                            "mint_hot_region_registry_handle, mint_quarantine_policy.  "
                                            "If you add or remove a warden mint, update BOTH the constant "
                                            "AND this ceiling pin in the same edit.");

}  // namespace crucible::fixy::warden::self_test

// Calling a warden mint from the smoke test would issue real syscalls, so the
// body only forces the re-exported names to resolve in a runtime context.

namespace crucible::fixy::warden {

inline void runtime_smoke_test() noexcept {
    constexpr bool admits_cold = CtxFitsHardeningMint<::crucible::effects::ColdInitCtx>;
    constexpr bool rejects_bg = !CtxFitsHardeningMint<::crucible::effects::BgDrainCtx>;
    (void)admits_cold;
    (void)rejects_bg;
}

}  // namespace crucible::fixy::warden
