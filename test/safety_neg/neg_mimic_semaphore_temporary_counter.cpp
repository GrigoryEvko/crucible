// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The descriptor borrows its counter.  A temporary counter dangles at the end
// of the full expression, so the mint form that takes it is deleted.

#include <crucible/mimic/Semaphore.h>

#include <atomic>
#include <cstdint>

int main() {
    const auto semaphore =
        crucible::mimic::mint_device_semaphore<crucible::mimic::VendorBackend::CPU>(std::atomic<std::uint64_t>{0}, 1);
    semaphore.signal(1);
    return 0;
}
