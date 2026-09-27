// mint_owned_file opens a stream, and an open is IO.  A background drain
// context owns Bg and Alloc and no IO, so the gate refuses it.

#include <fixy/OwnedFile.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

int main() {
    fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>> const ctx{fe::testing::bg()};
    auto stream = fixy::mint_owned_file(ctx, "/dev/null", "r");
    return stream.has_value() ? 0 : 1;
}
