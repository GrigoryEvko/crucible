// The compile-time checks of crucible/warden/Registry.h.

#include <crucible/warden/Registry.h>

#include <fixy/Ctx.h>

#include <type_traits>

namespace crucible::warden {

static_assert(sizeof(HotRegionRegistryHandle) == 1, "HotRegionRegistryHandle must be the 1-byte authorization token; "
                                                    "the underlying registry state lives in the Pinned singleton.");

static_assert(CtxFitsHotRegionRegistryMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsHotRegionRegistryMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsHotRegionRegistryMint<::fixy::HotFgCtx>);
static_assert(!std::is_default_constructible_v<HotRegionRegistryHandle>,
              "A handle built without the mint would carry an authorization nobody checked.");

// The registry is a static of the process with no end at exit.  A withdrawal
// from a destructor that runs after the end of main then finds a live list.
// The tag of the lock of the registry is private, so a probe tag stands in
// for it: the tag adds no member.
struct RegistryLockProbeTag final {};
static_assert(std::is_trivially_destructible_v<HotRegionRegistry::Snapshot>
                  && std::is_trivially_destructible_v<
                      ::fixy::spin::BlockingLocked<HotRegionRegistry::Snapshot, RegistryLockProbeTag>>,
              "The list and the lock of the registry must end with no code, so the static of the registry has no end "
              "at exit.");

}  // namespace crucible::warden
