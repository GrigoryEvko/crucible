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
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
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

// The value of a take that gives no item.  Each test pushes only smaller
// values.
constexpr std::uint64_t kNoItem = ~std::uint64_t{0};

// The item of a take, or kNoItem.
template <class T>
[[nodiscard]] std::uint64_t item_or_none(::fixy::Option<T> taken) noexcept {
    return std::move(taken).match([](T item) noexcept { return std::uint64_t{item}; },
                                  []() noexcept { return kNoItem; });
}

}  // namespace

using namespace fixy::concurrent;

using TestDeque = ChaseLevDeque<uint64_t, 256>;
using TestOwner = TestDeque::OwnerEnd;
using TestStealer = TestDeque::StealerEnd;

// The tag of a snapshot of the deque.  The test context holds Nondet.
static constexpr ::foundation::effects::cap::Nondet kSnapshot = ::foundation::effects::testing::test().nondet;

static_assert(!std::is_copy_constructible_v<TestDeque>, "ChaseLevDeque must not be copyable (Pinned contract)");
static_assert(!std::is_move_constructible_v<TestDeque>, "ChaseLevDeque must not be movable (interior atomics)");
static_assert(TestDeque::capacity() == 256);

// The ends of fixy/concurrent/ChaseLevDeque.h.  The OwnerEnd moves to another
// thread, and no two threads read one.  A StealerEnd copies, and two threads
// can use one at the same time.  The deque itself is Synchronized by its
// cells, so a Ref of it moves to a thief.
static_assert(!std::is_copy_constructible_v<TestOwner> && std::is_move_constructible_v<TestOwner>);
static_assert(::foundation::transfer::Sendable<TestOwner> && !::foundation::transfer::Shareable<TestOwner>);
static_assert(std::is_copy_constructible_v<TestStealer> && ::foundation::transfer::Synchronized<TestStealer>);
static_assert(::foundation::transfer::Sendable<TestStealer> && ::foundation::transfer::Synchronized<TestDeque>);

struct NotTriviallyCopyable {
    int x;
    NotTriviallyCopyable(const NotTriviallyCopyable&) {}
};
static_assert(!DequeValue<NotTriviallyCopyable>);

// A pointer fits a lock-free cell, but it moves an object to a thief with no
// transfer check, so a deque refuses it.
static_assert(!DequeValue<int*>);
static_assert(DequeValue<uint64_t>);

static void test_empty_deque() {
    ChaseLevDeque<uint64_t, 64> dq;
    auto ends = mint_chase_lev_ends(dq);
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.empty_approx(kSnapshot));
    CRUCIBLE_TEST_ASSERT(kTest, ends.stealer.size_approx(kSnapshot) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.pop_bottom().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, ends.stealer.steal_top().is_none());
    ::foundation::linear::drop(std::move(ends.owner));
    ::fixy::check::pass(kTest, "  test_empty_deque: PASSED\n");
}

static void test_single_thread_lifo() {
    ChaseLevDeque<uint64_t, 64> dq;
    auto ends = mint_chase_lev_ends(dq);

    for (uint64_t i = 0; i < 32; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(i));
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.size_approx(kSnapshot) == 32);

    // The owner's own end is last in, first out.
    for (uint64_t i = 0; i < 32; ++i) {
        const uint64_t expected = 31 - i;
        CRUCIBLE_TEST_ASSERT(kTest, item_or_none(ends.owner.pop_bottom()) == expected);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.empty_approx(kSnapshot));
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.pop_bottom().is_none());

    ::foundation::linear::drop(std::move(ends.owner));
    ::fixy::check::pass(kTest, "  test_single_thread_lifo: PASSED\n");
}

static void test_single_thread_steal_fifo() {
    ChaseLevDeque<uint64_t, 64> dq;
    auto ends = mint_chase_lev_ends(dq);

    for (uint64_t i = 0; i < 32; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(i));
    }
    // The thief's end is the opposite one, so it drains oldest first.  A
    // copy of the StealerEnd steals from the same deque.
    ChaseLevDeque<uint64_t, 64>::StealerEnd const thief = ends.stealer;
    for (uint64_t i = 0; i < 32; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, item_or_none(thief.steal_top()) == i);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends.stealer.steal_top().is_none());

    ::foundation::linear::drop(std::move(ends.owner));
    ::fixy::check::pass(kTest, "  test_single_thread_steal_fifo: PASSED\n");
}

static void test_capacity_bound() {
    ChaseLevDeque<uint64_t, 8> dq;
    auto ends = mint_chase_lev_ends(dq);
    for (uint64_t i = 0; i < 8; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(i));
    }
    CRUCIBLE_TEST_ASSERT(kTest, !ends.owner.push_bottom(99));
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.size_approx(kSnapshot) == 8);

    // Freeing one slot has to make room again, so the refusal above is
    // about occupancy and not a latched state.
    CRUCIBLE_TEST_ASSERT(kTest, item_or_none(ends.owner.pop_bottom()) == 7);
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(99));
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.size_approx(kSnapshot) == 8);

    ::foundation::linear::drop(std::move(ends.owner));
    ::fixy::check::pass(kTest, "  test_capacity_bound: PASSED\n");
}

static void test_interleaved_single_thread() {
    ChaseLevDeque<uint64_t, 32> dq;
    auto ends = mint_chase_lev_ends(dq);

    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(1));
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(2));
    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.push_bottom(3));

    // Stealing from the owning thread is unusual but permitted, and it
    // takes from the far end just as a thief would.
    CRUCIBLE_TEST_ASSERT(kTest, item_or_none(ends.stealer.steal_top()) == 1);
    CRUCIBLE_TEST_ASSERT(kTest, item_or_none(ends.owner.pop_bottom()) == 3);
    CRUCIBLE_TEST_ASSERT(kTest, item_or_none(ends.stealer.steal_top()) == 2);

    CRUCIBLE_TEST_ASSERT(kTest, ends.owner.empty_approx(kSnapshot));

    ::foundation::linear::drop(std::move(ends.owner));
    ::fixy::check::pass(kTest, "  test_interleaved_single_thread: PASSED\n");
}

// The mint takes the claim of the owner for the life of the deque, so a
// second mint of one deque ends the process.
static void mint_the_ends_two_times() {
    ChaseLevDeque<uint64_t, 8> dq;
    auto first = mint_chase_lev_ends(dq);
    auto second = mint_chase_lev_ends(dq);
    ::foundation::linear::drop(std::move(second.owner));
    ::foundation::linear::drop(std::move(first.owner));
}

static void test_one_owner() {
    ::fixy::check::expect_dies(kTest, mint_the_ends_two_times,
                               ::fixy::TextView{"a second OwnerEnd of one work-stealing"});
    ::fixy::check::pass(kTest, "  test_one_owner: PASSED\n");
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
    auto ends = mint_chase_lev_ends(dq);
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
    // Gives the item of a take to receive, and false when the take gave none.
    auto receive_taken = [&receive](::fixy::Option<uint64_t> taken) {
        std::uint64_t const item = item_or_none(std::move(taken));
        if (item == kNoItem) return false;
        receive(item);
        return true;
    };

    // The OwnerEnd moves into the closure of the owner thread, which ends it.
    std::jthread owner([&, owner_end = std::move(ends.owner)](std::stop_token /*st*/) mutable {
        for (uint64_t i = 0; i < N_ITEMS; ++i) {
            // A full queue means the thieves are behind.  The owner
            // takes from its own end while it waits, so the retry
            // cannot deadlock even if every thief stalls.
            while (!owner_end.push_bottom(i)) {
                static_cast<void>(receive_taken(owner_end.pop_bottom()));
                CRUCIBLE_SPIN_PAUSE;
            }
            // These interleaved takes are what put the owner and a
            // thief on the same element, which is the race the whole
            // protocol exists to resolve.
            if ((i & 0x3F) == 0) {
                static_cast<void>(receive_taken(owner_end.pop_bottom()));
            }
        }
        while (receive_taken(owner_end.pop_bottom())) {}
        ::foundation::linear::drop(std::move(owner_end));
        owner_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> thieves;
    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&, stealer = ends.stealer](std::stop_token /*st*/) {
            while (true) {
                if (receive_taken(stealer.steal_top())) continue;
                empty_steal_count.fetch_add(1, std::memory_order_relaxed);
                if (owner_done.load(std::memory_order_acquire) && stealer.empty_approx(kSnapshot)) {
                    // The emptiness query is approximate, so a
                    // thief only leaves after one more steal comes
                    // back with nothing.  Trusting the query alone
                    // would strand the last item.
                    if (!receive_taken(stealer.steal_top())) break;
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
    auto ends = mint_chase_lev_ends(dq);
    std::atomic<bool> owner_done{false};
    std::atomic<uint64_t> duplicate_count{0};

    std::vector<std::atomic<bool>> markers(N_ITEMS);
    for (auto& m : markers)
        m.store(false, std::memory_order_relaxed);

    auto receive = [&](std::uint64_t item) {
        if (item >= N_ITEMS) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool prev = markers[item].exchange(true, std::memory_order_relaxed);
        if (prev) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
        }
    };
    // Gives the item of a take to receive, and false when the take gave none.
    auto receive_taken = [&receive](::fixy::Option<uint32_t> taken) {
        std::uint64_t const item = item_or_none(std::move(taken));
        if (item == kNoItem) return false;
        receive(item);
        return true;
    };

    std::jthread owner([&, owner_end = std::move(ends.owner)](std::stop_token /*st*/) mutable {
        for (uint32_t i = 0; i < N_ITEMS; ++i) {
            while (!owner_end.push_bottom(i)) {
                static_cast<void>(receive_taken(owner_end.pop_bottom()));
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        while (receive_taken(owner_end.pop_bottom())) {}
        ::foundation::linear::drop(std::move(owner_end));
        owner_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> thieves;
    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&, stealer = ends.stealer](std::stop_token /*st*/) {
            while (true) {
                if (receive_taken(stealer.steal_top())) continue;
                if (owner_done.load(std::memory_order_acquire) && stealer.empty_approx(kSnapshot)) {
                    if (!receive_taken(stealer.steal_top())) break;
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

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_chase_lev_deque:\n");

    test_empty_deque();
    test_single_thread_lifo();
    test_single_thread_steal_fifo();
    test_capacity_bound();
    test_interleaved_single_thread();
    test_one_owner();
    test_stress_one_owner_n_thieves();
    test_stress_high_contention();

    ::fixy::check::pass(kTest, "test_chase_lev_deque: ALL PASSED\n");
    return 0;
}
