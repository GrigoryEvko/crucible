// A background context may also allocate, syscall or block, and none of
// the three belongs inside a section another thread is spinning on.  The
// context here owns Block as well, so it could wait on the blocking gate:
// owning Block does not open the spin gate to it.  The guard's
// constructor carries the clause so the diagnostic lands at the
// construction rather than one layer down.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace eff = foundation::effects;

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    BgCtx ctx{eff::testing::bg()};
    fixy::spin::SpinLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    fixy::spin::SpinGuard guard{ctx, gate, proof};
    return 0;
}
