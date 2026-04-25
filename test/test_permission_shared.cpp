// ═══════════════════════════════════════════════════════════════════
// test_permission_shared — SharedPermission + Pool + Guard (SEPLOG-A2)
//
// Coverage:
//   Tier 1: structural — sizeof, copy/move semantics (compile-time)
//   Tier 2: single-thread — Pool lend, Guard refcount, try_upgrade
//   Tier 3: mode transition — lend-vs-upgrade race resolution
//   Tier 4: multi-thread stress — N readers + 1 writer alternating modes
//   Tier 5: with_shared_read scoped helper
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace crucible::safety;

// ── Test harness ─────────────────────────────────────────────────

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
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

// Tags for test channels.
struct ConfigRegion {};
struct MetricsRegion {};

// ── Tier 1: compile-time structural properties ───────────────────

void test_compile_time_properties() {
    // SharedPermission: empty + copyable.
    static_assert(sizeof(SharedPermission<ConfigRegion>) == 1);
    static_assert(std::is_copy_constructible_v<SharedPermission<ConfigRegion>>);
    static_assert(std::is_trivially_copyable_v<SharedPermission<ConfigRegion>>);

    // Guard: move-only, exactly pointer-sized.
    static_assert(!std::is_copy_constructible_v<SharedPermissionGuard<ConfigRegion>>);
    static_assert(std::is_move_constructible_v<SharedPermissionGuard<ConfigRegion>>);
    static_assert(sizeof(SharedPermissionGuard<ConfigRegion>) == sizeof(void*));

    // Pool: Pinned (non-copyable, non-movable).
    static_assert(!std::is_copy_constructible_v<SharedPermissionPool<ConfigRegion>>);
    static_assert(!std::is_move_constructible_v<SharedPermissionPool<ConfigRegion>>);
}

// ── Tier 2: single-thread Pool semantics ──────────────────────────

void test_pool_lend_basic() {
    auto exc = permission_root_mint<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    {
        auto guard1 = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard1.has_value());
        CRUCIBLE_TEST_REQUIRE(guard1->holds_share());
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        // Token is just a copyable proof — getting it doesn't change state.
        SharedPermission<ConfigRegion> token = guard1->token();
        (void)token;
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        // Multiple guards co-exist.
        auto guard2 = pool.lend();
        auto guard3 = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard2.has_value());
        CRUCIBLE_TEST_REQUIRE(guard3.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 3);
    }
    // All guards destruct → refcount returns to 0.
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_guard_move_semantics() {
    auto exc = permission_root_mint<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    {
        auto g1 = pool.lend();
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        // Move construct — refcount unchanged.
        SharedPermissionGuard<ConfigRegion> g2{std::move(*g1)};
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);
        CRUCIBLE_TEST_REQUIRE(g2.holds_share());
        // Source is moved-from; no longer holds.
        CRUCIBLE_TEST_REQUIRE(!g1->holds_share());
        // g1's destructor will be a no-op (pool_ == nullptr).
    }
    // g2 destructed → refcount drops.
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_try_upgrade_succeeds_when_idle() {
    auto exc = permission_root_mint<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto recovered = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(recovered.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.is_exclusive_out());

    // After upgrade, lend MUST fail.
    auto fail_guard = pool.lend();
    CRUCIBLE_TEST_REQUIRE(!fail_guard.has_value());

    // Re-deposit; lend works again.
    pool.deposit_exclusive(std::move(*recovered));
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());
    auto post_guard = pool.lend();
    CRUCIBLE_TEST_REQUIRE(post_guard.has_value());
}

void test_try_upgrade_fails_when_outstanding() {
    auto exc = permission_root_mint<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto guard = pool.lend();
    CRUCIBLE_TEST_REQUIRE(guard.has_value());

    // While at least one share is out, upgrade must fail.
    auto upgrade = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(!upgrade.has_value());
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());
    // The guard remains valid.
    CRUCIBLE_TEST_REQUIRE(guard->holds_share());
}

// ── Tier 3: mode transition under contention ──────────────────────
//
// 8 threads all looping (lend → small work → release → try_upgrade →
// deposit_back → repeat).  Verifies the atomic state machine never
// observes:
//   * Two simultaneous "exclusive holders"
//   * A successful upgrade when outstanding > 0
//   * A successful lend when EXCLUSIVE_OUT_BIT is set
//
// Tracked by a parallel std::atomic counter that the test mutates
// inside the critical sections; if the protocol fails, the counter
// will exceed 1 in exclusive mode or fall behind in shared mode.

void test_lend_vs_upgrade_no_simultaneity() {
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS  = 5000;

    auto exc = permission_root_mint<MetricsRegion>();
    SharedPermissionPool<MetricsRegion> pool{std::move(exc)};

    // "Number of threads logically inside an exclusive section."
    // MUST never exceed 1.  Plain int (we only increment/check INSIDE
    // exclusive sections, where exclusivity is guaranteed).
    int  in_exclusive_count = 0;

    // "Number of threads logically inside a shared section."  May be
    // > 1 (that's the point) but MUST be 0 whenever in_exclusive_count
    // is being incremented.  Tracked atomically since multiple readers
    // may touch it concurrently.
    std::atomic<int> in_shared_count{0};

    // Tracks max simultaneous shared holders observed (sanity check
    // that we did exercise concurrency).
    std::atomic<int> max_shared_seen{0};

    // Track whether any invariant was violated.
    std::atomic<bool> violation{false};

    std::vector<std::jthread> workers;
    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([&pool, &in_exclusive_count, &in_shared_count,
                              &max_shared_seen, &violation, t](std::stop_token) {
            for (int i = 0; i < ITERATIONS; ++i) {
                // Half the threads are biased toward read; half toward
                // write.  This keeps both modes heavily exercised.
                const bool prefer_write = (t % 2 == 0);

                if (prefer_write && (i % 4 == 0)) {
                    // Try upgrade.  May fail; that's OK.
                    auto upgrade = pool.try_upgrade();
                    if (upgrade) {
                        // We're inside the exclusive section.  No reader
                        // should be present.
                        if (in_shared_count.load(std::memory_order_acquire) != 0) {
                            violation.store(true, std::memory_order_release);
                        }
                        ++in_exclusive_count;
                        if (in_exclusive_count != 1) {
                            violation.store(true, std::memory_order_release);
                        }
                        // Simulate work.
                        // Yield to widen the race window without volatile.
                        std::this_thread::yield();
                        --in_exclusive_count;
                        pool.deposit_exclusive(std::move(*upgrade));
                    }
                } else {
                    // Try lend.  May fail (writer is up); that's OK.
                    auto guard = pool.lend();
                    if (guard) {
                        const int now = in_shared_count.fetch_add(1,
                                            std::memory_order_acq_rel) + 1;
                        // Update max-seen with a CAS loop.
                        int seen = max_shared_seen.load(std::memory_order_relaxed);
                        while (now > seen &&
                               !max_shared_seen.compare_exchange_weak(
                                   seen, now,
                                   std::memory_order_acq_rel,
                                   std::memory_order_relaxed)) {}
                        // No exclusive holder may be present.
                        if (pool.is_exclusive_out()) {
                            violation.store(true, std::memory_order_release);
                        }
                        std::this_thread::yield();
                        in_shared_count.fetch_sub(1, std::memory_order_acq_rel);
                        // guard's destructor decrements pool refcount.
                    }
                }
            }
        });
    }
    for (auto& w : workers) w.join();

    CRUCIBLE_TEST_REQUIRE(!violation.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(in_exclusive_count == 0);
    CRUCIBLE_TEST_REQUIRE(in_shared_count.load() == 0);
    // Diagnostic: max simultaneous shared readers observed.  Should
    // typically be > 1 with 8 threads, but scheduler can in principle
    // serialise everything on a quiet machine; not a correctness check.
    (void)max_shared_seen.load();
    // Pool ends in deposit_exclusive state (writer finished and put
    // it back) OR with a stale upgraded-out (last writer didn't get
    // back to deposit before exit).  Either is fine for this test.
}

// ── Tier 4: multi-thread N-reader + 1-writer ──────────────────────
//
// 1 dedicated writer thread (occasionally upgrades, mutates a
// counter, deposits back).  N reader threads continuously lend +
// read.  Verifies readers never see a half-written state.

struct GuardedCounter {
    // Two halves that the writer increments together; readers verify
    // they always read consistent halves.  This is the SWMR pattern
    // the Pool is designed to protect.
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

void test_swmr_sees_consistent_state() {
    constexpr int NUM_READERS = 6;
    constexpr int ITERATIONS  = 1000;

    auto exc = permission_root_mint<MetricsRegion>();
    SharedPermissionPool<MetricsRegion> pool{std::move(exc)};
    GuardedCounter counter;  // protected by the SharedPermission protocol

    std::atomic<bool> writer_done{false};
    std::atomic<bool> reader_violation{false};
    std::atomic<std::uint64_t> reader_iters_total{0};

    std::jthread writer([&pool, &counter, &writer_done](std::stop_token) {
        for (int i = 0; i < ITERATIONS; ++i) {
            // Spin until upgrade succeeds.
            for (;;) {
                auto upgrade = pool.try_upgrade();
                if (upgrade) {
                    // Inside exclusive critical section: write both
                    // halves consistently.
                    const std::uint64_t v = static_cast<std::uint64_t>(i) + 1;
                    counter.lo = v;
                    counter.hi = v;
                    pool.deposit_exclusive(std::move(*upgrade));
                    break;
                }
                std::this_thread::yield();
            }
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back([&pool, &counter, &writer_done,
                              &reader_violation, &reader_iters_total]
                             (std::stop_token) {
            std::uint64_t local_iters = 0;
            while (!writer_done.load(std::memory_order_acquire)) {
                auto guard = pool.lend();
                if (!guard) {
                    // Writer is up; spin briefly.
                    std::this_thread::yield();
                    continue;
                }
                // Inside shared critical section: lo and hi MUST be
                // equal (writer always writes them together).
                const std::uint64_t lo = counter.lo;
                const std::uint64_t hi = counter.hi;
                if (lo != hi) {
                    reader_violation.store(true, std::memory_order_release);
                }
                ++local_iters;
                // guard's destructor releases the share.
            }
            reader_iters_total.fetch_add(local_iters, std::memory_order_acq_rel);
        });
    }

    writer.join();
    for (auto& r : readers) r.join();

    CRUCIBLE_TEST_REQUIRE(!reader_violation.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(counter.lo == counter.hi);
    CRUCIBLE_TEST_REQUIRE(counter.lo == ITERATIONS);
    // Sanity: readers should have made progress.
    CRUCIBLE_TEST_REQUIRE(reader_iters_total.load() > 0);
}

// ── Tier 5: with_shared_read scoped helper ────────────────────────

void test_with_shared_read_helper() {
    auto exc = permission_root_mint<ConfigRegion>();
    SharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    // Non-void body: returns a value through optional.
    auto result_opt = with_shared_read(pool,
        [](SharedPermission<ConfigRegion>) noexcept {
            return 42;
        });
    CRUCIBLE_TEST_REQUIRE(result_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(*result_opt == 42);
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);  // released

    // Void body: returns bool.
    bool ran = false;
    bool ok = with_shared_read(pool,
        [&ran](SharedPermission<ConfigRegion>) noexcept {
            ran = true;
        });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);

    // After upgrade-out, with_shared_read must fail (lend returns nullopt).
    auto upgrade = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(upgrade.has_value());
    auto failed_opt = with_shared_read(pool,
        [](SharedPermission<ConfigRegion>) noexcept { return 99; });
    CRUCIBLE_TEST_REQUIRE(!failed_opt.has_value());
    pool.deposit_exclusive(std::move(*upgrade));
}

// ── permission_share (untracked one-shot conversion) ──────────────

void test_permission_share() {
    auto exc = permission_root_mint<ConfigRegion>();
    auto shared = permission_share(std::move(exc));
    static_assert(std::is_same_v<decltype(shared), SharedPermission<ConfigRegion>>);
    // Copyable now.
    SharedPermission<ConfigRegion> shared2 = shared;
    SharedPermission<ConfigRegion> shared3 = shared2;
    (void)shared3;
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permission_shared:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_pool_lend_basic",                 test_pool_lend_basic);
    run_test("test_guard_move_semantics",            test_guard_move_semantics);
    run_test("test_try_upgrade_succeeds_when_idle",  test_try_upgrade_succeeds_when_idle);
    run_test("test_try_upgrade_fails_when_outstanding", test_try_upgrade_fails_when_outstanding);
    run_test("test_lend_vs_upgrade_no_simultaneity", test_lend_vs_upgrade_no_simultaneity);
    run_test("test_swmr_sees_consistent_state",      test_swmr_sees_consistent_state);
    run_test("test_with_shared_read_helper",         test_with_shared_read_helper);
    run_test("test_permission_share",                test_permission_share);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
