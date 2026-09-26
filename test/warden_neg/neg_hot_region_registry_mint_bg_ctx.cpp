// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hot_region_registry_handle refuses the background drain context.
// Registering a hot region is init work: the table backs the lock and
// huge-page walk of Hardening::apply().  A drain context that registered
// or withdrew a region would race an apply() in progress.

#include <crucible/warden/Registry.h>

int main() {
    ::fixy::BgDrainCtx const bg{::foundation::effects::testing::bg()};
    auto handle = crucible::warden::mint_hot_region_registry_handle(bg);
    (void)handle;
    return 0;
}
