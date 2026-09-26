// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hot_region_registry_handle refuses a type that is not an
// execution context at all.  IsExecCtx<Ctx> fails before the row check
// is attempted, so the gate refuses more than a context with the wrong
// row.

#include <crucible/warden/Registry.h>

struct NotAnExecCtx {};

int main() {
    auto handle = crucible::warden::mint_hot_region_registry_handle(NotAnExecCtx{});
    (void)handle;
    return 0;
}
