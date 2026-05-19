// FIXY-U-084 HS14 strengthening fixture (8 of 9).
//
// Demonstrates the SECOND mismatch class for mint_hot_region_registry_
// handle: a raw struct fails `IsExecCtx<Ctx>` BEFORE the row check is
// ever attempted (concept &&-short-circuits on the first false).  This
// proves the gate rejects not just "wrong-row ExecCtx instantiations"
// but also "non-ExecCtx types entirely".

#include <crucible/warden/Registry.h>

struct NotAnExecCtx {};

int main() {
    auto handle = crucible::warden::mint_hot_region_registry_handle(
        NotAnExecCtx{});
    (void)handle;
    return 0;
}
