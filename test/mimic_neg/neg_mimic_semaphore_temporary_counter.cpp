// Negative-compile test. This file must fail to compile.
//
// The descriptor borrows its counter.  A temporary counter dangles at the end
// of the full expression, so the mint form that takes it is deleted.

#include <crucible/mimic/Semaphore.h>

int main() {
    const auto semaphore = crucible::mimic::mint_device_semaphore<crucible::mimic::VendorBackend::CPU>(
        crucible::mimic::SemaphoreCounter{0}, 1);
    semaphore.signal(1);
    return 0;
}
