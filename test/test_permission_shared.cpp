#include <crucible/permissions/Permission.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>
#include <vector>

using namespace crucible::safety;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

struct ConfigRegion {};
struct MetricsRegion {};

void test_compile_time_properties() {
    static_assert(sizeof(SharedPermission<ConfigRegion>) == 1);
    static_assert(std::is_copy_constructible_v<SharedPermission<ConfigRegion>>);
    static_assert(std::is_trivially_copyable_v<SharedPermission<ConfigRegion>>);

    static_assert(!std::is_copy_constructible_v<SharedPermissionGuard<ConfigRegion>>);
    static_assert(std::is_move_constructible_v<SharedPermissionGuard<ConfigRegion>>);
    static_assert(sizeof(SharedPermissionGuard<ConfigRegion>) == sizeof(void*));

    static_assert(!std::is_copy_constructible_v<SharedPermissionPool<ConfigRegion>>);
    static_assert(!std::is_move_constructible_v<SharedPermissionPool<ConfigRegion>>);
}

void test_pool_lend_basic() {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    {
        auto guard1 = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard1.has_value());
        CRUCIBLE_TEST_REQUIRE(guard1->holds_share());
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        // The token is a copyable proof, so taking one does not change
        // the count.
        SharedPermission<ConfigRegion> token = guard1->token();
        (void)token;
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        auto guard2 = pool.lend();
        auto guard3 = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard2.has_value());
        CRUCIBLE_TEST_REQUIRE(guard3.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 3);
    }
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_guard_move_semantics() {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    {
        auto g1 = pool.lend();
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        SharedPermissionGuard<ConfigRegion> g2{std::move(*g1)};
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);
        CRUCIBLE_TEST_REQUIRE(g2.holds_share());
        CRUCIBLE_TEST_REQUIRE(!g1->holds_share());
        // The moved-from guard destructs without decrementing.
    }
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_try_upgrade_succeeds_when_idle() {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto recovered = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(recovered.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.is_exclusive_out());

    auto fail_guard = pool.lend();
    CRUCIBLE_TEST_REQUIRE(!fail_guard.has_value());

    pool.deposit_exclusive(std::move(*recovered));
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());
    auto post_guard = pool.lend();
    CRUCIBLE_TEST_REQUIRE(post_guard.has_value());
}

void test_try_upgrade_fails_when_outstanding() {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto guard = pool.lend();
    CRUCIBLE_TEST_REQUIRE(guard.has_value());

    auto upgrade = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(!upgrade.has_value());
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());
    CRUCIBLE_TEST_REQUIRE(guard->holds_share());
}

// Eight threads alternate lend and upgrade.  The parallel counters
// below catch two simultaneous exclusive holders, an upgrade granted
// while shares are out, or a lend granted while a writer holds.

void test_lend_vs_upgrade_no_simultaneity() {
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 5000;

    auto exc = mint_permission_root<MetricsRegion>();
    SharedPermissionPool<MetricsRegion> pool{std::move(exc)};

    // Never exceeds 1.  A plain int is safe because it is only touched
    // inside exclusive sections.
    int in_exclusive_count = 0;

    // May exceed 1, but must be 0 whenever a writer is inside.  Atomic
    // because several readers touch it at once.
    std::atomic<int> in_shared_count{0};

    // Confirms the test actually saw concurrency.
    std::atomic<int> max_shared_seen{0};

    std::atomic<bool> violation{false};

    std::vector<std::jthread> workers;
    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back(
            [&pool, &in_exclusive_count, &in_shared_count, &max_shared_seen, &violation, t](std::stop_token) {
                for (int i = 0; i < ITERATIONS; ++i) {
                    // Half the threads are biased toward read; half toward
                    // write.  This keeps both modes heavily exercised.
                    const bool prefer_write = (t % 2 == 0);

                    if (prefer_write && (i % 4 == 0)) {
                        auto upgrade = pool.try_upgrade();
                        if (upgrade) {
                            if (in_shared_count.load(std::memory_order_acquire) != 0) {
                                violation.store(true, std::memory_order_release);
                            }
                            ++in_exclusive_count;
                            if (in_exclusive_count != 1) {
                                violation.store(true, std::memory_order_release);
                            }
                            // Widen the race window.
                            CRUCIBLE_SPIN_PAUSE;
                            --in_exclusive_count;
                            pool.deposit_exclusive(std::move(*upgrade));
                        }
                    } else {
                        auto guard = pool.lend();
                        if (guard) {
                            const int now = in_shared_count.fetch_add(1, std::memory_order_acq_rel) + 1;
                            int seen = max_shared_seen.load(std::memory_order_relaxed);
                            while (now > seen
                                   && !max_shared_seen.compare_exchange_weak(seen, now, std::memory_order_acq_rel,
                                                                             std::memory_order_relaxed)) {}
                            if (pool.is_exclusive_out()) {
                                violation.store(true, std::memory_order_release);
                            }
                            CRUCIBLE_SPIN_PAUSE;
                            in_shared_count.fetch_sub(1, std::memory_order_acq_rel);
                            // The guard's destructor releases the share.
                        }
                    }
                }
            });
    }
    for (auto& w : workers)
        w.join();

    CRUCIBLE_TEST_REQUIRE(!violation.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(in_exclusive_count == 0);
    CRUCIBLE_TEST_REQUIRE(in_shared_count.load() == 0);
    // Not a correctness check: a quiet machine can serialise everything.
    (void)max_shared_seen.load();
    // The pool can end either deposited or still upgraded out, so there
    // is no final-state assertion here.
}

// Readers must never see a half-written counter.

struct GuardedCounter {
    // The writer sets both halves together.  A reader that sees them
    // differ has observed a torn write.
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

void test_swmr_sees_consistent_state() {
    constexpr int NUM_READERS = 6;
    constexpr int ITERATIONS = 1000;
    // Each reader does this many iterations regardless of writer_done.
    // Without a floor, an aggressive scheduler can finish the writer
    // before any reader runs, leaving the count at zero.  That would
    // make the assertion depend on scheduler fairness, which the pool
    // does not promise.
    constexpr int MIN_READER_ITERS = 32;

    auto exc = mint_permission_root<MetricsRegion>();
    SharedPermissionPool<MetricsRegion> pool{std::move(exc)};
    GuardedCounter counter;

    std::atomic<bool> writer_done{false};
    std::atomic<bool> reader_violation{false};
    std::atomic<std::uint64_t> reader_iters_total{0};

    // All threads rendezvous first, so the writer cannot run to
    // completion before any reader starts.
    std::latch start_latch{NUM_READERS + 1};

    std::jthread writer([&pool, &counter, &writer_done, &start_latch](std::stop_token) {
        start_latch.arrive_and_wait();
        for (int i = 0; i < ITERATIONS; ++i) {
            for (;;) {
                auto upgrade = pool.try_upgrade();
                if (upgrade) {
                    const std::uint64_t v = static_cast<std::uint64_t>(i) + 1;
                    counter.lo = v;
                    counter.hi = v;
                    pool.deposit_exclusive(std::move(*upgrade));
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back(
            [&pool, &counter, &writer_done, &reader_violation, &reader_iters_total, &start_latch](std::stop_token) {
                start_latch.arrive_and_wait();

                // True only if this iteration acquired a guard.
                auto do_one_iter = [&]() noexcept {
                    auto guard = pool.lend();
                    if (!guard) return false;
                    const std::uint64_t lo = counter.lo;
                    const std::uint64_t hi = counter.hi;
                    if (lo != hi) {
                        reader_violation.store(true, std::memory_order_release);
                    }
                    return true;
                };

                std::uint64_t local_iters = 0;

                // This loop is unbounded, and terminates because every
                // deposit clears the exclusive bit, which lets a waiting
                // reader's compare-exchange succeed.
                while (local_iters < MIN_READER_ITERS) {
                    if (do_one_iter()) {
                        ++local_iters;
                    } else {
                        CRUCIBLE_SPIN_PAUSE;
                    }
                }

                while (!writer_done.load(std::memory_order_acquire)) {
                    if (do_one_iter()) {
                        ++local_iters;
                    } else {
                        CRUCIBLE_SPIN_PAUSE;
                    }
                }

                reader_iters_total.fetch_add(local_iters, std::memory_order_acq_rel);
            });
    }

    writer.join();
    for (auto& r : readers)
        r.join();

    CRUCIBLE_TEST_REQUIRE(!reader_violation.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(counter.lo == counter.hi);
    CRUCIBLE_TEST_REQUIRE(counter.lo == ITERATIONS);
    CRUCIBLE_TEST_REQUIRE(reader_iters_total.load() >= static_cast<std::uint64_t>(NUM_READERS) * MIN_READER_ITERS);
}

void test_with_shared_read_helper() {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto result_opt = with_shared_read(pool, [](SharedPermission<ConfigRegion>) noexcept { return 42; });
    CRUCIBLE_TEST_REQUIRE(result_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(*result_opt == 42);
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);

    // A void body makes the helper return bool.
    bool ran = false;
    bool ok = with_shared_read(pool, [&ran](SharedPermission<ConfigRegion>) noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);

    auto upgrade = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(upgrade.has_value());
    auto failed_opt = with_shared_read(pool, [](SharedPermission<ConfigRegion>) noexcept { return 99; });
    CRUCIBLE_TEST_REQUIRE(!failed_opt.has_value());
    pool.deposit_exclusive(std::move(*upgrade));
}

// A one-shot conversion with no pool tracking.
void test_mint_permission_share() {
    auto exc = mint_permission_root<ConfigRegion>();
    auto shared = mint_permission_share(std::move(exc));
    static_assert(std::is_same_v<decltype(shared), SharedPermission<ConfigRegion>>);
    // Copyable now.
    SharedPermission<ConfigRegion> shared2 = shared;
    SharedPermission<ConfigRegion> shared3 = shared2;
    (void)shared3;
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_mint_permission_shared:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_pool_lend_basic", test_pool_lend_basic);
    run_test("test_guard_move_semantics", test_guard_move_semantics);
    run_test("test_try_upgrade_succeeds_when_idle", test_try_upgrade_succeeds_when_idle);
    run_test("test_try_upgrade_fails_when_outstanding", test_try_upgrade_fails_when_outstanding);
    run_test("test_lend_vs_upgrade_no_simultaneity", test_lend_vs_upgrade_no_simultaneity);
    run_test("test_swmr_sees_consistent_state", test_swmr_sees_consistent_state);
    run_test("test_with_shared_read_helper", test_with_shared_read_helper);
    run_test("test_mint_permission_share", test_mint_permission_share);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
