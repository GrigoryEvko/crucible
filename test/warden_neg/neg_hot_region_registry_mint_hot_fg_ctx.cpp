// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hot_region_registry_handle refuses the hot foreground context.
// Its row is empty, so it holds no capability to write registry state.
// The registry operations are bounded, but a hot context that could
// reach them would invite a registry walk at hot-path cadence.

#include <crucible/warden/Registry.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::HotFgCtx const fg = ::foundation::effects::testing::foreground();
    auto handle = crucible::warden::mint_hot_region_registry_handle(fg);
    (void)handle;
    return 0;
}
