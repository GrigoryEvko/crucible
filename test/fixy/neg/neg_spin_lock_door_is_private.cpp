// lock() is private, per design note 1 of fixy/os/SpinLock.h.  The only
// way in is the guard, whose clause refuses a context that may not wait on
// the gate.  A public lock() would let a caller acquire the gate with no
// context, and the clause of the guard would not apply to that caller.

#include <fixy/os/SpinLock.h>

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    fixy::spin::SpinLock<GateTag> gate{};
    gate.lock();
    return 0;
}
