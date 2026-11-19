// The header ships its own static_asserts, which run only when a translation
// unit includes it under the warning and standard flags of the project.  This
// file does that, and it operates a minted descriptor at run time.
//
// Each device family uses the host oracle, so each case below operates the
// descriptor of every family that the mint admits.

#include <crucible/mimic/Semaphore.h>
#include <foundation/Platform.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace mimic = crucible::mimic;
using mimic::VendorBackend;

namespace {

// A signal of a value lets a wait for that value or a smaller one pass, and a
// wait for a larger value fails.
template <VendorBackend Backend>
void signal_then_wait() {
    mimic::SemaphoreCounter counter{0};
    const auto semaphore = mimic::mint_device_semaphore<Backend>(counter, 41);
    static_assert(decltype(semaphore)::backend == Backend);
    CRUCIBLE_TEST_ASSERT(kTest, semaphore.native_handle() == 41);
    CRUCIBLE_TEST_ASSERT(kTest, !semaphore.try_wait(1));
    semaphore.signal(5);
    CRUCIBLE_TEST_ASSERT(kTest, semaphore.try_wait(5));
    CRUCIBLE_TEST_ASSERT(kTest, semaphore.try_wait(4));
    CRUCIBLE_TEST_ASSERT(kTest, !semaphore.try_wait(6));
    CRUCIBLE_TEST_ASSERT(kTest, counter.load_acquire() == 5);
}

void test_signal_then_wait() {
    signal_then_wait<VendorBackend::CPU>();
    signal_then_wait<VendorBackend::NV>();
    signal_then_wait<VendorBackend::AMD>();
    signal_then_wait<VendorBackend::TPU>();
    signal_then_wait<VendorBackend::TRN>();
    signal_then_wait<VendorBackend::CER>();
    ::fixy::check::pass(kTest, "  test_signal_then_wait:        PASSED\n");
}

// A copy and an assignment keep the counter and the handle of their source,
// so two holders operate one counter.
void test_copy_and_assignment() {
    mimic::SemaphoreCounter first_counter{0};
    mimic::SemaphoreCounter second_counter{0};
    const auto first = mimic::mint_device_semaphore<VendorBackend::CPU>(first_counter, 1);
    auto second = mimic::mint_device_semaphore<VendorBackend::CPU>(second_counter, 2);
    const auto copied = first;
    copied.signal(3);
    CRUCIBLE_TEST_ASSERT(kTest, first.try_wait(3));
    second = first;
    CRUCIBLE_TEST_ASSERT(kTest, second.native_handle() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, second.try_wait(3));
    CRUCIBLE_TEST_ASSERT(kTest, second_counter.load_acquire() == 0);
    ::fixy::check::pass(kTest, "  test_copy_and_assignment:     PASSED\n");
}

// A waiter on another thread that sees the signal also sees the write that
// the signaler made before the signal.
void test_signal_publishes_prior_writes() {
    mimic::SemaphoreCounter counter{0};
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
    CRUCIBLE_TEST_ASSERT(kTest, observed == 99);
    ::fixy::check::pass(kTest, "  test_signal_publishes_prior_writes: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_mimic_semaphore:\n");
    test_signal_then_wait();
    test_copy_and_assignment();
    test_signal_publishes_prior_writes();
    ::fixy::check::pass(kTest, "test_mimic_semaphore: all PASSED\n");
    return 0;
}
