// An open can wait in the kernel on the file system, so mint_owned_file
// asks for Block beside IO.  A context that owns IO and not Block is
// refused.

#include <fixy/OwnedFile.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

int main() {
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const ctx{fe::testing::test()};
    auto stream = fixy::mint_owned_file(ctx, "/dev/null", "r");
    return stream.has_value() ? 0 : 1;
}
