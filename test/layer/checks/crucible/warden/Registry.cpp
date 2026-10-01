// The compile-time checks of crucible/warden/Registry.h.

#include <crucible/warden/Registry.h>

namespace crucible::warden {

static_assert(sizeof(HotRegionRegistryHandle) == 1, "HotRegionRegistryHandle must be the 1-byte authorization token; "
                                                    "the underlying registry state lives in the Pinned singleton.");

static_assert(CtxFitsHotRegionRegistryMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsHotRegionRegistryMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsHotRegionRegistryMint<::fixy::HotFgCtx>);
static_assert(!std::is_default_constructible_v<HotRegionRegistryHandle>,
              "A handle built without the mint would carry an authorization nobody checked.");

}  // namespace crucible::warden
