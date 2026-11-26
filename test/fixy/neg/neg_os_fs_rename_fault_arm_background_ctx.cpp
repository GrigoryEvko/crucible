// RenameFaultScope::arm takes a context that owns Test, so a context of the
// background source is refused at its signature.  A thread of a product holds
// no test context, so it cannot start a rename fault.

#include <fixy/os/Fs.h>

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
    fs::RenameFaultScope const fault = fs::RenameFaultScope::arm(ctx, fs::RenameFailure::not_supported);
    return 0;
}
