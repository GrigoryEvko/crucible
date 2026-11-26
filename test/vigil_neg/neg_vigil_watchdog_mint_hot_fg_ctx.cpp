// Negative-compile test.  This file must fail to compile.
//
// The hot foreground context records ops, and it loads no program and starts
// no thread.  So mint_vigil_with_deadline_watchdog refuses it, as mint_vigil
// does, and a Vigil with a watchdog comes only from the startup load context.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::HotFgCtx const foreground = ::foundation::effects::testing::foreground();
    crucible::Vigil vigil =
        crucible::mint_vigil_with_deadline_watchdog(foreground, {}, ::crucible::warden::Policy::production());
    return vigil.watchdog_enabled() ? 0 : 1;
}
