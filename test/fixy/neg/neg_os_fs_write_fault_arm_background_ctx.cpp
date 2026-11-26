// WriteFaultScope::arm takes a context that owns Test, so a context of the
// background source is refused at its signature.  A thread of a product holds
// no test context, so it cannot start a write fault.

#include <fixy/os/Fs.h>

#include <foundation/core/Quantity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
using BackgroundCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    BackgroundCtx const ctx{eff::testing::bg()};
    fs::WriteFaultScope const fault =
        fs::WriteFaultScope::arm(ctx, foundation::core::ByteCount{0}, fs::WriteFailure::no_space);
    return 0;
}
