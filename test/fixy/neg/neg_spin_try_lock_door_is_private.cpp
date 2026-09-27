// try_lock() is private for the same reason lock() is: reaching it
// without a context is the bypass fixy/os/SpinLock.h closes.  The try
// guard is the way to try.

#include <fixy/os/SpinLock.h>

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    fixy::spin::SpinLock<GateTag> gate{};
    [[maybe_unused]] const bool got = gate.try_lock();
    return 0;
}
