// Tests of the futex door of fixy/os/Futex.h: the end of each wait, the
// wakes, the deadline, and a signal that reaches a thread that waits.  The
// tsan preset runs the same binary, and the signal test shows there that the
// route of the primitive marks a futex wait as a blocking call (THE
// SANITIZERS of foundation/os/Syscall.h).

#include <fixy/os/Futex.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Time.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

namespace {

// The count of the runs of the handler of the signal test.  A store to an
// atomic word is safe in a handler.
constinit ::foundation::core::Atomic<std::uint32_t> handler_runs{0};

void record_the_signal(int) noexcept { static_cast<void>(handler_runs.fetch_add_acq_rel(1)); }

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a signal reaches a thread that waits in the futex door")
#include <pthread.h>
#include <signal.h>

namespace {

// Installs the handler of SIGUSR1 with SA_RESTART.  The kernel then starts a
// futex wait that the signal ends again, and the thread that waits does not
// return to its code until a wake.  The handler then runs only inside the
// wait.
[[nodiscard]] bool install_the_handler() noexcept {
    struct ::sigaction action{};
    action.sa_handler = record_the_signal;
    action.sa_flags = SA_RESTART;
    static_cast<void>(::sigemptyset(&action.sa_mask));
    return ::sigaction(SIGUSR1, &action, nullptr) == 0;
}

// The id of a thread is an unsigned long on x86_64 and aarch64, so it fits in
// a word of 64 bits.
[[nodiscard]] std::uint64_t calling_thread() noexcept { return ::pthread_self(); }

[[nodiscard]] bool send_the_signal(std::uint64_t thread) noexcept { return ::pthread_kill(thread, SIGUSR1) == 0; }

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace eff = ::foundation::effects;
namespace core = ::foundation::core;
namespace futex = ::fixy::futex;
using ::fixy::ClockSource_v;
using ::fixy::thread::ThreadOptions;
using futex::Deadline;
using futex::FutexWord;
using futex::WaitEnd;

using BlockCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

constexpr std::uint64_t kMillisecond = 1000000U;

// The deadline BOUND_NANOS after now.
[[nodiscard]] Deadline deadline_after(BlockCtx const& ctx, std::uint64_t bound_nanos) noexcept {
    auto const clock = ::fixy::time::mint_clock_reader<ClockSource_v::Monotonic>(ctx);
    return Deadline::after(clock, bound_nanos).expect("the monotonic clock gives a reading");
}

// A word that does not hold the expected value ends the wait at once.
void test_a_changed_word_ends_the_wait() {
    BlockCtx const ctx{eff::testing::bg()};
    FutexWord word{5};
    WaitEnd const end = futex::wait(ctx, word, 0).expect("a wait on a word of the process is valid");
    CRUCIBLE_FATAL_INVARIANT(end == WaitEnd::word_changed);
}

// A wake with no waiter wakes no thread.
void test_a_wake_with_no_waiter_wakes_none() {
    BlockCtx const ctx{eff::testing::bg()};
    FutexWord word{0};
    CRUCIBLE_FATAL_INVARIANT(futex::wake_one(ctx, word).expect("a wake is valid") == 0);
    CRUCIBLE_FATAL_INVARIANT(futex::wake_all(ctx, word).expect("a wake is valid") == 0);
}

// A wait with a deadline BOUND_NANOS after now ends when the clock passes the
// deadline.  A wait that the kernel ends with no cause waits again until the
// same deadline.
void expect_the_deadline_to_end_the_wait(std::uint64_t bound_nanos) {
    BlockCtx const ctx{eff::testing::bg()};
    FutexWord word{0};
    Deadline const deadline = deadline_after(ctx, bound_nanos);
    WaitEnd end = WaitEnd::woken;
    while (end == WaitEnd::woken || end == WaitEnd::interrupted) {
        end = futex::wait(ctx, word, 0, core::Option<Deadline>::some(deadline)).expect("a timed wait is valid");
    }
    CRUCIBLE_FATAL_INVARIANT(end == WaitEnd::deadline_passed);
}

// A deadline of one millisecond ends the wait, and a deadline that has
// passed ends it at once.
void test_a_deadline_ends_the_wait() {
    expect_the_deadline_to_end_the_wait(kMillisecond);
    expect_the_deadline_to_end_the_wait(0);
}

// A waiter sleeps until a change of the word and a wake.  The change comes
// before the wake, so a wake before the sleep is not lost: the sleep then
// finds the changed word.
void test_a_wake_ends_the_wait() {
    BlockCtx const ctx{eff::testing::bg()};
    FutexWord word{0};
    core::Atomic<bool> is_waiter_done{false};
    auto waiter = [&]() noexcept {
        while (word.load_acquire() == 0) {
            static_cast<void>(futex::wait(ctx, word, 0).expect("a wait on a word of the process is valid"));
        }
        is_waiter_done.store_release(true);
    };
    auto waker = [&]() noexcept {
        word.store_release(1);
        static_cast<void>(futex::wake_all(ctx, word).expect("a wake is valid"));
    };
    CRUCIBLE_FATAL_INVARIANT(::fixy::thread::run_joined(ctx, ThreadOptions{}, waiter, waker).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_waiter_done.load_acquire());
}

// A signal reaches a thread that waits with no deadline, and its handler
// runs during the wait.  The signaling thread gives the waiter 50 ms to
// start its wait first.  It then waits at most 10 s for the handler, and it
// wakes the waiter in each case, so a lost signal fails the test and does
// not hang it.
void test_a_signal_reaches_a_waiter() {
    CRUCIBLE_FATAL_INVARIANT(install_the_handler());
    BlockCtx const ctx{eff::testing::bg()};
    FutexWord word{0};
    core::Atomic<std::uint64_t> waiter_thread{0};
    core::Atomic<bool> was_handler_seen{false};
    auto waiter = [&]() noexcept {
        waiter_thread.store_release(calling_thread());
        while (word.load_acquire() == 0) {
            static_cast<void>(futex::wait(ctx, word, 0).expect("a wait on a word of the process is valid"));
        }
    };
    auto signaler = [&]() noexcept {
        std::uint64_t thread = waiter_thread.load_acquire();
        while (thread == 0) {
            CRUCIBLE_SPIN_PAUSE;
            thread = waiter_thread.load_acquire();
        }
        expect_the_deadline_to_end_the_wait(50 * kMillisecond);
        CRUCIBLE_FATAL_INVARIANT(send_the_signal(thread));
        for (int step = 0; step < 10000 && handler_runs.load_acquire() == 0; ++step) {
            static_cast<void>(
                futex::wait(ctx, handler_runs, 0, core::Option<Deadline>::some(deadline_after(ctx, kMillisecond)))
                    .expect("a timed wait is valid"));
        }
        was_handler_seen.store_release(handler_runs.load_acquire() != 0);
        word.store_release(1);
        static_cast<void>(futex::wake_all(ctx, word).expect("a wake is valid"));
    };
    CRUCIBLE_FATAL_INVARIANT(::fixy::thread::run_joined(ctx, ThreadOptions{}, waiter, signaler).is_ok());
    CRUCIBLE_FATAL_INVARIANT(was_handler_seen.load_acquire());
}

}  // namespace

int main() {
    test_a_changed_word_ends_the_wait();
    test_a_wake_with_no_waiter_wakes_none();
    test_a_deadline_ends_the_wait();
    test_a_wake_ends_the_wait();
    test_a_signal_reaches_a_waiter();
    return 0;
}
