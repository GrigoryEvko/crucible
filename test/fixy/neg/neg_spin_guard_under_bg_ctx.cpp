// A background context that owns IO or Block could make a system call or
// sleep inside a section another thread is spinning on.  The context here
// owns Div, so the spin itself is permitted, and only its IO and Block
// close the spin gate.  The guard's constructor carries the clause so the
// diagnostic lands at the construction rather than one layer down.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace eff = foundation::effects;

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx =
    eff::ExecCtx<eff::Bg,
                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Div>>;
}  // namespace

int main() {
    BgCtx ctx{eff::testing::bg()};
    fixy::spin::SpinLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    fixy::spin::SpinGuard guard{ctx, gate, proof};
    return 0;
}
