// What fixy/handle/OneShotFlag.h claims, checked.
//
// The flag fills a cache line.  A raise is run once by a check.  A raise
// that arrives while the body runs is kept, so the next check runs the
// body again.  Several consumers share the raises, and no raise runs two
// bodies.  An acquire peek that reads true sees the producer's writes.
// A reset names its proof of quiescence (the two fixtures
// neg_handle_one_shot_flag_reset_* hold that).

#include <fixy/handle/OneShotFlag.h>

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
static_assert(std::is_same_v<decltype(std::declval<h::OneShotFlag&>().peek_acquire()), bool>);

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
        while (!flag.peek_acquire()) {}
        seen = payload;
    });
    payload = 42;
    flag.signal();
    consumer.join();
    return seen == 42;
}

[[nodiscard]] bool a_quiescent_reset_takes_the_flag_down() noexcept {
    h::OneShotFlag flag;
    flag.signal();
    flag.reset_in_quiescent_context(h::OneShotFlag::QuiescenceProof{});
    return !flag.peek() && !flag.check_and_run([]() noexcept {});
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
    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_handle_one_shot_flag: no raise is lost and no raise runs twice\n");
    return EXIT_SUCCESS;
}
