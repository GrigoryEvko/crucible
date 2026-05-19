// FIXY-U-084 HS14 neg-compile fixture (4 of 6).
//
// mint_hot_region_registry_handle rejects HotFgCtx.  HotFgCtx's row
// is empty (Row<>) — the hot foreground has no capability to touch
// registry state.  Registry CAS operations are bounded but bounded-
// is-not-free; admitting Hot ctxs here would invite per-iteration
// registry walks at hot-path cadence.

#include <crucible/warden/Registry.h>

int main() {
    auto handle = crucible::warden::mint_hot_region_registry_handle(
        crucible::effects::HotFgCtx{});
    (void)handle;
    return 0;
}
