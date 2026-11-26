// Negative-compile test.  This file must fail to compile.
//
// The hot foreground context records ops, and it starts no thread and seals
// no table.  So mint_vigil refuses it, and a Vigil comes only from the
// startup load context.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::HotFgCtx const foreground = ::foundation::effects::testing::foreground();
    crucible::Vigil vigil = crucible::mint_vigil(foreground, {});
    return vigil.is_compiled() ? 0 : 1;
}
