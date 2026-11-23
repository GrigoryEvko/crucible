// A class takes a bare lock by hand, as a class beside its data did.  The
// lock then guards no data that the type names, and a member that reads the
// data with no lock compiles.  The lock and the unlock of a bare lock are
// private, and only Gate, Locked and its guard take one.

#include <fixy/os/SpinLock.h>

int main() {
    ::fixy::spin::UnwitnessedBlockingLock lock{};
    lock.lock();
    return 0;
}
