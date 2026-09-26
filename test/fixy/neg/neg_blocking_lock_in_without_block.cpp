// The manual door of a blocking gate carries the same clause the guard
// does.  A foreground context owns no Block, so it cannot sleep on the
// gate through lock_in either.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace eff = foundation::effects;

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
}  // namespace

int main() {
    ForegroundCtx ctx{eff::testing::test()};
    fixy::spin::BlockingLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    gate.lock_in(ctx, proof);
    gate.unlock(proof);
    return 0;
}
