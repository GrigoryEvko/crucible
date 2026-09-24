// try_lock() is private for the same reason lock() is: reaching it
// without a context is the bypass fixy/os/SpinLock.h closes.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    fixy::spin::SpinLock<GateTag> gate{};
    auto proof = foundation::permissions::mint_permission_root<GateTag>();
    [[maybe_unused]] const bool got = gate.try_lock(proof);
    return 0;
}
