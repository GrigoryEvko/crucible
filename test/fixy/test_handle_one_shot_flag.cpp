// What fixy/handle/OneShotFlag.h claims, checked.
//
// The flag fills a cache line.  A raise is run once by a check.  A raise
// that arrives while the body runs is kept, so the next check runs the
// body again.  Several consumers share the raises, and no raise runs two
// bodies.  An acquire peek that reads true sees the producer's writes.
// A reset takes a proof of quiescence that only the OwnerThread of the owner
// of the flag gives (the fixtures neg_handle_one_shot_flag_* hold that), and
// the proof of another owner ends the process.

#include <fixy/handle/OneShotFlag.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/OwnedThread.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <utility>

namespace h = fixy::handle;

namespace {

static_assert(alignof(h::OneShotFlag) >= 64 && sizeof(h::OneShotFlag) >= 64);
static_assert(!std::is_copy_constructible_v<h::OneShotFlag> && !std::is_move_constructible_v<h::OneShotFlag>);
static_assert(std::is_same_v<decltype(std::declval<h::OneShotFlag&>().peek()), bool>);

[[nodiscard]] bool a_raise_runs_the_body_once() noexcept {
    h::OneShotFlag flag;
    int runs = 0;
    if (flag.check_and_run([&]() noexcept { ++runs; })) return false;
    flag.signal();
    const bool first = flag.check_and_run([&]() noexcept { ++runs; });
    const bool second = flag.check_and_run([&]() noexcept { ++runs; });
    return first && !second && runs == 1 && !flag.peek();
}

// The body raises the flag again, as a producer that signals while the
// body runs would.  A check that stored false after the body would lose
// that raise.
[[nodiscard]] bool a_raise_during_the_body_is_kept() noexcept {
    h::OneShotFlag flag;
    int runs = 0;
    flag.signal();
    const bool first = flag.check_and_run([&]() noexcept {
        ++runs;
        if (runs == 1) flag.signal();
    });
    const bool raised_after = flag.peek();
    const bool second = flag.check_and_run([&]() noexcept { ++runs; });
    return first && raised_after && second && runs == 2 && !flag.peek();
}

// Two consumers race for one raise, many times.  Each raise runs one
// body, never two.
[[nodiscard]] bool one_raise_runs_one_body_under_two_consumers() noexcept {
    constexpr int kRounds = 2000;
    for (int round = 0; round < kRounds; ++round) {
        h::OneShotFlag flag;
        std::atomic<int> runs{0};
        std::atomic<bool> go{false};
        const auto consumer = [&] {
            while (!go.load(std::memory_order_acquire)) {}
            (void)flag.check_and_run([&]() noexcept { runs.fetch_add(1, std::memory_order_relaxed); });
        };
        std::thread first(consumer);
        std::thread second(consumer);
        flag.signal();
        go.store(true, std::memory_order_release);
        first.join();
        second.join();
        if (runs.load(std::memory_order_relaxed) != 1) return false;
    }
    return true;
}

[[nodiscard]] bool an_acquire_peek_sees_the_producer_write() noexcept {
    h::OneShotFlag flag;
    int payload = 0;
    int seen = 0;
    std::thread consumer([&] {
        while (!flag.peek()) {}
        seen = payload;
    });
    payload = 42;
    flag.signal();
    consumer.join();
    return seen == 42;
}

// An owner of a flag and of the thread that reads it.  Its OwnerThread is the
// one door to the proof that no thread of the owner runs.  The thread and the
// starter use only the flag, which two threads can use at once, and the
// OwnerThread is private, so the owner states synchronized{}
// (foundation/Transfer.h).
class[[= ::foundation::transfer::synchronized{}]] SignalOwner {
public:
    SignalOwner() = default;
    SignalOwner(SignalOwner const&) = delete("the thread borrows its owner at one address");
    SignalOwner(SignalOwner&&) = delete("the thread borrows its owner at one address");
    SignalOwner& operator=(SignalOwner const&) = delete("the thread borrows its owner at one address");
    SignalOwner& operator=(SignalOwner&&) = delete("the thread borrows its owner at one address");
    ~SignalOwner() = default;

    // The key of the door that starts the thread.  The thread that holds the
    // owner makes it, and the key stays on that thread (THE PROOF OF A DOOR
    // of foundation/Transfer.h).
    struct[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] StarterKey final {};

    // The proof of the OwnerThread of this owner, for the owner that the
    // caller names.  A test names another owner to show the refusal.
    [[nodiscard]] ::fixy::thread::QuiescenceProof<SignalOwner> quiescence_for(SignalOwner const& owner) const noexcept {
        return thread_.quiescence(owner);
    }

    // Starts the thread of the owner.  The body waits for the stop that the
    // end of the OwnerThread signals.
    void start_waiting_thread(StarterKey& /*key*/) noexcept {
        ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
        ::fixy::thread::mint_owner_thread<&SignalOwner::thread_, &SignalOwner::request_stop>(
            bg, ::fixy::thread::ThreadOptions{}, *this,
            [](SignalOwner& self) noexcept {
                while (!self.stop.peek()) {
                    CRUCIBLE_SPIN_PAUSE;
                }
            })
            .expect("the thread door starts the thread of the owner");
    }

    h::OneShotFlag stop;

private:
    // The stop hook of the thread, which writes only the flag.
    void request_stop() noexcept { stop.signal(); }

    ::fixy::thread::OwnerThread<SignalOwner> thread_{};
};

[[nodiscard]] bool a_quiescent_reset_takes_the_flag_down() noexcept {
    SignalOwner owner{};
    owner.stop.signal();
    owner.stop.reset_in_quiescent_context(owner.quiescence_for(owner));
    return !owner.stop.peek() && !owner.stop.check_and_run([]() noexcept {});
}

// The proof names its owner, so the proof of one owner resets no flag of
// another owner.
void a_reset_with_the_proof_of_another_owner_ends_the_process() noexcept {
    SignalOwner first{};
    SignalOwner second{};
    second.stop.reset_in_quiescent_context(first.quiescence_for(first));
}

// The proof names the owner of the OwnerThread that gives it.
void a_proof_of_an_owner_that_does_not_hold_the_thread_ends_the_process() noexcept {
    SignalOwner first{};
    SignalOwner second{};
    second.stop.reset_in_quiescent_context(first.quiescence_for(second));
}

// A named proof outlives a later start of the thread of its owner.  The thread
// then reads the flag, so a reset with that proof ends the process.
void a_reset_with_a_proof_of_before_the_start_ends_the_process() noexcept {
    SignalOwner owner{};
    ::fixy::thread::QuiescenceProof<SignalOwner> const before_the_start = owner.quiescence_for(owner);
    SignalOwner::StarterKey key{};
    owner.start_waiting_thread(key);
    owner.stop.reset_in_quiescent_context(before_the_start);
}

}  // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool holds, const char* claim) {
        if (!holds) {
            std::fprintf(stderr, "FAIL: %s\n", claim);
            ++failures;
        }
    };
    check(a_raise_runs_the_body_once(), "one raise runs the body once, and the check takes the flag down");
    check(a_raise_during_the_body_is_kept(), "a raise that arrives while the body runs is kept for the next check");
    check(one_raise_runs_one_body_under_two_consumers(), "two consumers never run two bodies for one raise");
    check(an_acquire_peek_sees_the_producer_write(), "an acquire peek that reads true sees the producer's write");
    check(a_quiescent_reset_takes_the_flag_down(), "a quiescent reset takes a raised flag down");
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, a_reset_with_the_proof_of_another_owner_ends_the_process,
                               ::fixy::TextView{"a reset took the proof of quiescence of an owner that does not hold"});
    ::fixy::check::expect_dies(
        test, a_proof_of_an_owner_that_does_not_hold_the_thread_ends_the_process,
        ::fixy::TextView{"a proof of quiescence of an owner that does not hold the OwnerThread"});
    ::fixy::check::expect_dies(test, a_reset_with_a_proof_of_before_the_start_ends_the_process,
                               ::fixy::TextView{"a proof of quiescence while the thread of the owner runs again"});
    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_handle_one_shot_flag: no raise is lost and no raise runs twice\n");
    return EXIT_SUCCESS;
}
