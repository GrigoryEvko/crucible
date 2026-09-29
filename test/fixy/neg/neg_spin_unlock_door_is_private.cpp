// unlock() is private, per design note 3 of fixy/os/SpinLock.h.  A
// public unlock that took any Permission of the tag would let a thread
// that does not hold the gate release it, and let one holder release it
// two times.  The guard releases once, and only after it acquired.

#include <fixy/os/SpinLock.h>

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
