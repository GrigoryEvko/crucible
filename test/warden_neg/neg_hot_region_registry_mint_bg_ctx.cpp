// FIXY-U-084 HS14 neg-compile fixture (3 of 6).
//
// mint_hot_region_registry_handle rejects BgDrainCtx.  Registering
// new hot regions is an Init-row act (the registry table backs
// Hardening::apply()'s mlock2 / MADV_HUGEPAGE walk).  Bg drain
// contexts must not register / unregister regions — that would race
// against an in-flight apply() and is also a structural confusion
// of the cold init-time setup with steady-state drain work.

#include <crucible/warden/Registry.h>

int main() {
    auto handle = crucible::warden::mint_hot_region_registry_handle(
        crucible::effects::BgDrainCtx{});
    (void)handle;
    return 0;
}
