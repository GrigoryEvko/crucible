// The concurrent tests need no reference implementation to compare
// against, because each item is its own witness.  Items are the numbers
// zero up to the count, each receiver marks the one it got, and after
// every thread has stopped two properties settle the question: every
// mark is set, and no mark was set twice.  A lost item means the queue
// dropped work; a repeated item means two threads were handed the same
// work.  Either one is an ordering defect and neither can hide.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/ChaseLevDeque.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace fixy::concurrent;

using TestDeque = ChaseLevDeque<uint64_t, 256>;

// The tag of a snapshot of the deque.  The test context holds Nondet.
static constexpr ::foundation::effects::cap::Nondet kSnapshot = ::foundation::effects::testing::test().nondet;

static_assert(!std::is_copy_constructible_v<TestDeque>, "ChaseLevDeque must not be copyable (Pinned contract)");
static_assert(!std::is_move_constructible_v<TestDeque>, "ChaseLevDeque must not be movable (interior atomics)");
static_assert(TestDeque::capacity() == 256);

struct NotTriviallyCopyable {
    int x;
    NotTriviallyCopyable(const NotTriviallyCopyable&) {}
};
static_assert(!DequeValue<NotTriviallyCopyable>);

// A pointer is admitted because it fits in a lock-free atomic, which
// is what the steal protocol needs of the payload.
static_assert(DequeValue<int*>);
static_assert(DequeValue<uint64_t>);

static void test_empty_deque() {
    ChaseLevDeque<uint64_t, 64> dq;
    CRUCIBLE_TEST_ASSERT(kTest, dq.empty_approx(kSnapshot));
    CRUCIBLE_TEST_ASSERT(kTest, dq.size_approx(kSnapshot) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, !dq.pop_bottom().has_value());
    CRUCIBLE_TEST_ASSERT(kTest, !dq.steal_top().has_value());
    ::fixy::check::pass(kTest, "  test_empty_deque: PASSED\n");
}

static void test_single_thread_lifo() {
    ChaseLevDeque<uint64_t, 64> dq;

    for (uint64_t i = 0; i < 32; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(i));
    }
    CRUCIBLE_TEST_ASSERT(kTest, dq.size_approx(kSnapshot) == 32);

    // The owner's own end is last in, first out.
    for (uint64_t i = 0; i < 32; ++i) {
        const uint64_t expected = 31 - i;
        const auto opt = dq.pop_bottom();
        CRUCIBLE_TEST_ASSERT(kTest, opt.has_value());
        CRUCIBLE_TEST_ASSERT(kTest, *opt == expected);
    }
    CRUCIBLE_TEST_ASSERT(kTest, dq.empty_approx(kSnapshot));
    CRUCIBLE_TEST_ASSERT(kTest, !dq.pop_bottom().has_value());

    ::fixy::check::pass(kTest, "  test_single_thread_lifo: PASSED\n");
}

static void test_single_thread_steal_fifo() {
    ChaseLevDeque<uint64_t, 64> dq;

    for (uint64_t i = 0; i < 32; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(i));
    }
    // The thief's end is the opposite one, so it drains oldest first.
    for (uint64_t i = 0; i < 32; ++i) {
        const auto opt = dq.steal_top();
        CRUCIBLE_TEST_ASSERT(kTest, opt.has_value());
        CRUCIBLE_TEST_ASSERT(kTest, *opt == i);
    }
    CRUCIBLE_TEST_ASSERT(kTest, !dq.steal_top().has_value());

    ::fixy::check::pass(kTest, "  test_single_thread_steal_fifo: PASSED\n");
}

static void test_capacity_bound() {
    ChaseLevDeque<uint64_t, 8> dq;
    for (uint64_t i = 0; i < 8; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(i));
    }
    CRUCIBLE_TEST_ASSERT(kTest, !dq.push_bottom(99));
    CRUCIBLE_TEST_ASSERT(kTest, dq.size_approx(kSnapshot) == 8);

    // Freeing one slot has to make room again, so the refusal above is
    // about occupancy and not a latched state.
    auto popped = dq.pop_bottom();
    CRUCIBLE_TEST_ASSERT(kTest, popped.has_value());
    CRUCIBLE_TEST_ASSERT(kTest, *popped == 7);
    CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(99));
    CRUCIBLE_TEST_ASSERT(kTest, dq.size_approx(kSnapshot) == 8);

    ::fixy::check::pass(kTest, "  test_capacity_bound: PASSED\n");
}

static void test_interleaved_single_thread() {
    ChaseLevDeque<uint64_t, 32> dq;

    CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(1));
    CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(2));
    CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(3));

    // Stealing from the owning thread is unusual but permitted, and it
    // takes from the far end just as a thief would.
    auto a = dq.steal_top();
    CRUCIBLE_TEST_ASSERT(kTest, a.has_value() && *a == 1);

    auto b = dq.pop_bottom();
    CRUCIBLE_TEST_ASSERT(kTest, b.has_value() && *b == 3);

    auto c = dq.steal_top();
    CRUCIBLE_TEST_ASSERT(kTest, c.has_value() && *c == 2);

    CRUCIBLE_TEST_ASSERT(kTest, dq.empty_approx(kSnapshot));

    ::fixy::check::pass(kTest, "  test_interleaved_single_thread: PASSED\n");
}

// The owner both produces and consumes here, so the contended element
// is the one at the boundary where its end meets the thieves' end.

static void test_stress_one_owner_n_thieves() {
    constexpr std::size_t N_ITEMS = 200000;
    constexpr int N_THIEVES = 4;
    constexpr std::size_t CAPACITY = 1024;

    ::fixy::report(::fixy::Sink::Out, "  test_stress_one_owner_n_thieves: {} items, {} thieves...\n", N_ITEMS,
                   N_THIEVES);

    ChaseLevDeque<uint64_t, CAPACITY> dq;
    std::atomic<bool> owner_done{false};
    std::atomic<uint64_t> duplicate_count{0};
    std::atomic<uint64_t> empty_steal_count{0};

    // One flag per item.  The exchange that sets a flag also reports
    // its previous state, so a second receiver of the same item is
    // detected at the moment it happens rather than after the fact.
    std::vector<std::atomic<bool>> markers(N_ITEMS);
    for (auto& m : markers)
        m.store(false, std::memory_order_relaxed);

    auto receive = [&](uint64_t item) {
        // A value outside the range was never pushed, so reading one
        // means an uninitialised cell was handed out.  It is counted
        // here rather than given a channel of its own.
        if (item >= N_ITEMS) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool prev = markers[item].exchange(true, std::memory_order_relaxed);
        if (prev) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::jthread owner([&](std::stop_token /*st*/) {
        for (uint64_t i = 0; i < N_ITEMS; ++i) {
            // A full queue means the thieves are behind.  The owner
            // takes from its own end while it waits, so the retry
            // cannot deadlock even if every thief stalls.
            while (!dq.push_bottom(i)) {
                if (auto opt = dq.pop_bottom()) {
                    receive(*opt);
                }
                CRUCIBLE_SPIN_PAUSE;
            }
            // These interleaved takes are what put the owner and a
            // thief on the same element, which is the race the whole
            // protocol exists to resolve.
            if ((i & 0x3F) == 0) {
                if (auto opt = dq.pop_bottom()) {
                    receive(*opt);
                }
            }
        }
        while (auto opt = dq.pop_bottom()) {
            receive(*opt);
        }
        owner_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> thieves;
    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&](std::stop_token /*st*/) {
            while (true) {
                if (auto opt = dq.steal_top()) {
                    receive(*opt);
                } else {
                    empty_steal_count.fetch_add(1, std::memory_order_relaxed);
                    if (owner_done.load(std::memory_order_acquire) && dq.empty_approx(kSnapshot)) {
                        // The emptiness query is approximate, so a
                        // thief only leaves after one more steal comes
                        // back with nothing.  Trusting the query alone
                        // would strand the last item.
                        if (auto last = dq.steal_top()) {
                            receive(*last);
                        } else {
                            break;
                        }
                    }
                }
            }
        });
    }

    // Both of these join.  The marks are only read afterwards.
    owner = std::jthread{};
    thieves.clear();

    std::size_t missing = 0;
    for (std::size_t i = 0; i < N_ITEMS; ++i) {
        if (!markers[i].load(std::memory_order_relaxed)) {
            ++missing;
        }
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);
    const uint64_t empty_steals = empty_steal_count.load(std::memory_order_relaxed);

    ::fixy::report(::fixy::Sink::Out,
                   "    items: {}, missing: {}, duplicates: {}\n"
                   "    empty steal attempts: {} (informational)\n",
                   N_ITEMS, missing, static_cast<unsigned long long>(dup),
                   static_cast<unsigned long long>(empty_steals));

    CRUCIBLE_TEST_ASSERT(kTest, missing == 0 && "item lost — owner pushed but no receiver marked it");
    CRUCIBLE_TEST_ASSERT(kTest, dup == 0 && "item duplicated — owner-vs-thief race resolution is broken");

    ::fixy::check::pass(kTest, "  test_stress_one_owner_n_thieves: PASSED\n");
}

// A small queue with more thieves keeps the two ends close together, so
// steals collide often and the retry path runs instead of being skipped
// over as it mostly is in the test above.

static void test_stress_high_contention() {
    constexpr std::size_t N_ITEMS = 50000;
    constexpr int N_THIEVES = 8;
    constexpr std::size_t CAPACITY = 32;

    ::fixy::report(::fixy::Sink::Out, "  test_stress_high_contention: {} items, {} thieves, capacity {}...\n", N_ITEMS,
                   N_THIEVES, CAPACITY);

    ChaseLevDeque<uint32_t, CAPACITY> dq;
    std::atomic<bool> owner_done{false};
    std::atomic<uint64_t> duplicate_count{0};

    std::vector<std::atomic<bool>> markers(N_ITEMS);
    for (auto& m : markers)
        m.store(false, std::memory_order_relaxed);

    auto receive = [&](uint32_t item) {
        if (item >= N_ITEMS) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool prev = markers[item].exchange(true, std::memory_order_relaxed);
        if (prev) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::jthread owner([&](std::stop_token /*st*/) {
        for (uint32_t i = 0; i < N_ITEMS; ++i) {
            while (!dq.push_bottom(i)) {
                if (auto opt = dq.pop_bottom()) {
                    receive(*opt);
                }
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        while (auto opt = dq.pop_bottom()) {
            receive(*opt);
        }
        owner_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> thieves;
    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&](std::stop_token /*st*/) {
            while (true) {
                if (auto opt = dq.steal_top()) {
                    receive(*opt);
                } else if (owner_done.load(std::memory_order_acquire) && dq.empty_approx(kSnapshot)) {
                    if (auto last = dq.steal_top()) {
                        receive(*last);
                    } else {
                        break;
                    }
                }
            }
        });
    }

    owner = std::jthread{};
    thieves.clear();

    std::size_t missing = 0;
    for (std::size_t i = 0; i < N_ITEMS; ++i) {
        if (!markers[i].load(std::memory_order_relaxed)) ++missing;
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);

    ::fixy::report(::fixy::Sink::Out, "    items: {}, missing: {}, duplicates: {}\n", N_ITEMS, missing,
                   static_cast<unsigned long long>(dup));

    CRUCIBLE_TEST_ASSERT(kTest, missing == 0 && "item lost under high contention");
    CRUCIBLE_TEST_ASSERT(kTest, dup == 0 && "item duplicated under high contention");

    ::fixy::check::pass(kTest, "  test_stress_high_contention: PASSED\n");
}

// A pointer payload is the shape real work items take, and it is also
// the width where the lock-free atomic guarantee has to hold.
static void test_pointer_payload() {
    ChaseLevDeque<int*, 64> dq;

    int values[10];
    for (int i = 0; i < 10; ++i) {
        values[i] = i * 100;
        CRUCIBLE_TEST_ASSERT(kTest, dq.push_bottom(&values[i]));
    }

    // The pointers come back in reverse, and each one still addresses
    // the value it was taken from.
    for (int i = 9; i >= 0; --i) {
        auto opt = dq.pop_bottom();
        CRUCIBLE_TEST_ASSERT(kTest, opt.has_value());
        CRUCIBLE_TEST_ASSERT(kTest, *opt == &values[i]);
        CRUCIBLE_TEST_ASSERT(kTest, **opt == i * 100);
    }

    ::fixy::check::pass(kTest, "  test_pointer_payload: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_chase_lev_deque:\n");

    test_empty_deque();
    test_single_thread_lifo();
    test_single_thread_steal_fifo();
    test_capacity_bound();
    test_interleaved_single_thread();
    test_pointer_payload();
    test_stress_one_owner_n_thieves();
    test_stress_high_contention();

    ::fixy::check::pass(kTest, "test_chase_lev_deque: ALL PASSED\n");
    return 0;
}
