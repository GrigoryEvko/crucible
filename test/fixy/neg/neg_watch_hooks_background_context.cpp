// The hooks of the session watch change the process for the rest of its
// life, so only a context that owns Init mints them.  A background context
// can start threads, but it owns no Init, so the gate of mint_watch_hooks
// refuses it.

#include <fixy/Ctx.h>
#include <fixy/session/Watch.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::BgDrainCtx const background{::foundation::effects::testing::bg()};
    [[maybe_unused]] auto const hooks = ::fixy::session::watch::mint_watch_hooks(background);
    return 0;
}
