// lock() is private, per deviation 1 of fixy/os/SpinLock.h: the only way
// in is lock_in<Ctx>, whose clause refuses a background context.  The old
// header had this door public, so eleven production sites took it and the
// clause never applied to them.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace {
struct GateTag {};
}  // namespace

int main() {
    fixy::spin::SpinLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    gate.lock(proof);
    return 0;
}
