// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The backend Portable names no one device, so a semaphore on it has no
// device to signal.  SemaphoreBackend refuses it in the clause of the class,
// before a mint is called.

#include <crucible/mimic/Semaphore.h>

int main() {
    // The size is the one use of the class, so the refusal is the only error.
    static_cast<void>(sizeof(crucible::mimic::DeviceSemaphore<crucible::mimic::VendorBackend::Portable>));
    return 0;
}
