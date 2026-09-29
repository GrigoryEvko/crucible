// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The backend Portable names no one device, so a semaphore on it has no
// device to signal.  SemaphoreBackend refuses it in the clause of the class,
// before a mint is called.

#include <crucible/mimic/Semaphore.h>

using PortableSemaphore = crucible::mimic::DeviceSemaphore<crucible::mimic::VendorBackend::Portable>;

int main() {
    const PortableSemaphore* semaphore = nullptr;
    return semaphore == nullptr ? 0 : 1;
}
