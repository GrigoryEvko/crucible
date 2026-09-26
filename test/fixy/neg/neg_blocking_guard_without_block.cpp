// Waiting on a blocking gate puts the waiter to sleep, so the wait is a
// block.  A background context that owns no Block may not take the gate:
// the guard's clause names the capability it lacks.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace eff = foundation::effects;

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgDrainCtx ctx{eff::testing::bg()};
    fixy::spin::BlockingLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    fixy::spin::BlockingGuard guard{ctx, gate, proof};
    return 0;
}
