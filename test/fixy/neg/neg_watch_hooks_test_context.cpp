// The hooks of the session watch change the process for the rest of its
// life, so only a context that owns Init mints them.  A test context owns no
// Init, so the gate of mint_watch_hooks refuses it.

#include <fixy/session/Watch.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const scope{fe::testing::test()};
    [[maybe_unused]] auto const hooks = ::fixy::session::watch::mint_watch_hooks(scope);
    return 0;
}
