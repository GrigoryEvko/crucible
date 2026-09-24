// unlock() takes the same Permission lock did, per deviation 3 of
// fixy/os/SpinLock.h.  A release that costs nothing lets a caller release
// a gate it never acquired.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    fixy::spin::SpinLock<GateTag> gate{};
    gate.unlock();
    return 0;
}
