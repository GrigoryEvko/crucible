// Negative-compile test.  This file must fail to compile.
//
// The start of a Vigil seals the schema table and the kernel table, which
// spins, and the watchdog loads its programs, which waits in the kernel.  The
// cold init context admits neither, so mint_vigil_with_deadline_watchdog
// refuses it, and the startup load context is the one context of a Vigil.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::ColdInitCtx const startup{::foundation::effects::testing::init()};
    crucible::Vigil vigil =
        crucible::mint_vigil_with_deadline_watchdog(startup, {}, ::crucible::warden::Policy::production());
    return vigil.watchdog_enabled() ? 0 : 1;
}
