// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of a DeviceSemaphore is private, and mint_device_semaphore
// is its one friend.  A caller cannot build a descriptor around the mint.

#include <crucible/mimic/Semaphore.h>

int main() {
    crucible::mimic::SemaphoreCounter counter{0};
    const crucible::mimic::DeviceSemaphore<crucible::mimic::VendorBackend::CPU> semaphore{counter, 1};
    semaphore.signal(1);
    return 0;
}
