// mint_temporary_file creates a stream in the file system, which is IO.
// The foreground context owns no effect, so the gate refuses it.

#include <fixy/OwnedFile.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

int main() {
    fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>> const ctx = fe::testing::foreground();
    auto stream = fixy::mint_temporary_file(ctx);
    return stream.has_value() ? 0 : 1;
}
