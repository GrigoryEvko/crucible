// A temporary stream is created in the file system, and the create can
// wait in the kernel, so mint_temporary_file asks for Block beside IO.  A
// context that owns IO and not Block is refused.

#include <fixy/OwnedFile.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

int main() {
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const ctx{fe::testing::test()};
    auto stream = fixy::mint_temporary_file(ctx);
    return stream.has_value() ? 0 : 1;
}
