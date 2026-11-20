// The singleton root mint takes the context rule of the plain root mint.
// The row of the tag names IO, and the context admits no IO, so the context
// does not own the region of the tag, and the mint refuses it.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace eff = ::foundation::effects;

// The tag has a stable identity, so only the context rule refuses the mint.
namespace neg_singleton_root_outside_the_ctx_row_types {
struct IoRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace neg_singleton_root_outside_the_ctx_row_types

using neg_singleton_root_outside_the_ctx_row_types::IoRegion;

namespace {
using BgOnlyCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;
}  // namespace

int main() {
    BgOnlyCtx ctx{eff::testing::bg()};
    [[maybe_unused]] auto token = ::foundation::permissions::mint_singleton_root<IoRegion>(ctx);
    return 0;
}
