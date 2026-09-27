// lock() is private, per deviation 1 of fixy/os/SpinLock.h: the only way
// in is the guard, whose clause refuses a context that may not wait on
// the gate.  The old header had this door public, so eleven production
// sites took it and the clause never applied to them.

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
