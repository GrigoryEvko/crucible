// Negative-compile test.  This file must fail to compile.
//
// A background context runs a stage of the pipeline of a Vigil that exists.
// It owns no Init atom, so mint_vigil refuses it, and a background thread
// starts no Vigil.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    crucible::Vigil vigil = crucible::mint_vigil(bg, {});
    return vigil.is_compiled() ? 0 : 1;
}
