// A cold init context owns Alloc and IO, and it owns no Bg.  A holder
// under it could allocate or make a system call while other threads spin
// on the gate, so the spin guard refuses it.  The blocking guard is the
// gate for such work.

#include <fixy/Ctx.h>
#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    fixy::ColdInitCtx ctx{foundation::effects::testing::init()};
    fixy::spin::SpinLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    fixy::spin::SpinGuard guard{ctx, gate, proof};
    return 0;
}
