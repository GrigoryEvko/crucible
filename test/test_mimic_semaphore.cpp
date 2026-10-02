// The header ships its own static_asserts, which run only when a translation
// unit includes it under the warning and standard flags of the project.  This
// file does that, and it operates a minted descriptor at run time.
//
// Each device family uses the host oracle, so each case below operates the
// descriptor of every family that the mint admits.

#include <crucible/mimic/Semaphore.h>
#include <foundation/Platform.h>

#include "test_assert.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace mimic = crucible::mimic;
using mimic::VendorBackend;

namespace {

// A signal of a value lets a wait for that value or a smaller one pass, and a
// wait for a larger value fails.
template <VendorBackend Backend>
void signal_then_wait() {
    std::atomic<std::uint64_t> counter{0};
    const auto semaphore = mimic::mint_device_semaphore<Backend>(counter, 41);
    static_assert(decltype(semaphore)::backend == Backend);
    assert(semaphore.native_handle() == 41);
    assert(!semaphore.try_wait(1));
    semaphore.signal(5);
    assert(semaphore.try_wait(5));
    assert(semaphore.try_wait(4));
    assert(!semaphore.try_wait(6));
    assert(counter.load(std::memory_order_acquire) == 5);
}

void test_signal_then_wait() {
    signal_then_wait<VendorBackend::CPU>();
    signal_then_wait<VendorBackend::NV>();
    signal_then_wait<VendorBackend::AMD>();
    signal_then_wait<VendorBackend::TPU>();
    signal_then_wait<VendorBackend::TRN>();
    signal_then_wait<VendorBackend::CER>();
    crucible::test::pass("  test_signal_then_wait:        PASSED\n");
}

// A copy and an assignment keep the counter and the handle of their source,
// so two holders operate one counter.
void test_copy_and_assignment() {
    std::atomic<std::uint64_t> first_counter{0};
    std::atomic<std::uint64_t> second_counter{0};
    const auto first = mimic::mint_device_semaphore<VendorBackend::CPU>(first_counter, 1);
    auto second = mimic::mint_device_semaphore<VendorBackend::CPU>(second_counter, 2);
    const auto copied = first;
    copied.signal(3);
    assert(first.try_wait(3));
    second = first;
    assert(second.native_handle() == 1);
    assert(second.try_wait(3));
    assert(second_counter.load(std::memory_order_acquire) == 0);
    crucible::test::pass("  test_copy_and_assignment:     PASSED\n");
}

// A waiter on another thread that sees the signal also sees the write that
// the signaler made before the signal.
void test_signal_publishes_prior_writes() {
    std::atomic<std::uint64_t> counter{0};
    const auto semaphore = mimic::mint_device_semaphore<VendorBackend::CPU>(counter, 7);
    std::uint64_t payload = 0;
    std::uint64_t observed = 0;
    {
        std::jthread waiter{[&semaphore, &payload, &observed] {
            while (!semaphore.try_wait(1)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            observed = payload;
        }};
        payload = 99;
        semaphore.signal(1);
    }
    assert(observed == 99);
    crucible::test::pass("  test_signal_publishes_prior_writes: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_mimic_semaphore:\n");
    test_signal_then_wait();
    test_copy_and_assignment();
    test_signal_publishes_prior_writes();
    crucible::test::pass("test_mimic_semaphore: all PASSED\n");
    return 0;
}
