// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The backend None names no kernel, so a semaphore on it has no device to
// signal.  SemaphoreBackend refuses it in the clause of the mint.

#include <crucible/mimic/Semaphore.h>

int main() {
    crucible::mimic::SemaphoreCounter counter{0};
    const auto semaphore = crucible::mimic::mint_device_semaphore<crucible::mimic::VendorBackend::None>(counter, 1);
    semaphore.signal(1);
    return 0;
}
