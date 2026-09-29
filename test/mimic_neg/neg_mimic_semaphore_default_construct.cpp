// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A default descriptor would point to no counter, and a signal through it
// would write through a null pointer.  The default constructor is deleted.

#include <crucible/mimic/Semaphore.h>

int main() {
    const crucible::mimic::DeviceSemaphore<crucible::mimic::VendorBackend::CPU> semaphore{};
    semaphore.signal(1);
    return 0;
}
