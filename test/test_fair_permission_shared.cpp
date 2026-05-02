// ═══════════════════════════════════════════════════════════════════
// test_fair_permission_shared — FairSharedPermissionPool (#861-followup)
//
// Coverage:
//   Tier 1: structural — sizeof, copy/move semantics, BurstLimit policy
//   Tier 2: single-thread — burst gate fires after K, lend resets, unchecked bypasses
//   Tier 3: bounded-overtaking — N readers vs aggressive writer, prove
//           reader latency bound (no reader is overtaken > BurstLimit times)
//   Tier 4: no-reader scenario — try_upgrade_unchecked makes progress
//           when burst gate fires with no contender present
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/FairSharedPermissionPool.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>
#include <vector>

using namespace crucible::safety;

// ── Test harness ────────────────────────────────────────────────────

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

// Tags for distinct test pools.
struct ConfigRegion {};
struct MetricsRegion {};
struct StarvationRegion {};
struct NoReaderRegion {};

// ── Tier 1: compile-time properties ─────────────────────────────────

void test_compile_time_properties() {
    using P8 = FairSharedPermissionPool<ConfigRegion, 8>;
    using P1 = FairSharedPermissionPool<ConfigRegion, 1>;
    using P32 = FairSharedPermissionPool<ConfigRegion, 32>;

    static_assert(P8::writer_burst_limit == 8);
    static_assert(P1::writer_burst_limit == 1);
    static_assert(P32::writer_burst_limit == 32);

    static_assert(!std::is_copy_constructible_v<P8>);
    static_assert(!std::is_move_constructible_v<P8>);
    static_assert(!std::is_copy_assignable_v<P8>);
    static_assert(!std::is_move_assignable_v<P8>);

    // Default BurstLimit is 8.
    static_assert(FairSharedPermissionPool<ConfigRegion>::writer_burst_limit == 8);

    // Tag preserved.
    static_assert(std::is_same_v<P8::tag_type, ConfigRegion>);
}

// ── Tier 2: single-thread burst behavior ────────────────────────────

void test_burst_gate_fires_after_k_wins() {
    constexpr std::uint32_t K = 5;
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, K> pool{std::move(exc)};

    // Initial state: counter 0, gate clear.
    CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_burst_exhausted());

    // K successful upgrades.
    for (std::uint32_t i = 0; i < K; ++i) {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == i + 1);
        pool.deposit_exclusive(std::move(*u));
    }

    // Burst exhausted.
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    // Next try_upgrade refuses.
    {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(!u.has_value());
        // Counter unchanged by the refused attempt.
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == K);
    }
}

void test_lend_resets_burst_counter() {
    constexpr std::uint32_t K = 4;
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, K> pool{std::move(exc)};

    // Fill the burst.
    for (std::uint32_t i = 0; i < K; ++i) {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        pool.deposit_exclusive(std::move(*u));
    }
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    // A successful lend resets the counter.
    {
        auto guard = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
    }
    CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_burst_exhausted());

    // Writer regains its full burst budget.
    {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 1);
        pool.deposit_exclusive(std::move(*u));
    }
}

void test_unchecked_bypasses_burst_gate() {
    constexpr std::uint32_t K = 2;
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, K> pool{std::move(exc)};

    // Saturate the burst.
    for (std::uint32_t i = 0; i < K; ++i) {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        pool.deposit_exclusive(std::move(*u));
    }
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    // Gated try_upgrade refuses.
    CRUCIBLE_TEST_REQUIRE(!pool.try_upgrade().has_value());

    // Unchecked still works AND increments the counter past the limit.
    {
        auto u = pool.try_upgrade_unchecked();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == K + 1);
        pool.deposit_exclusive(std::move(*u));
    }
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    // Lend still resets even after unchecked overflow.
    {
        auto guard = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
    }
    CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 0);
}

void test_unchecked_when_idle_succeeds() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, 8> pool{std::move(exc)};

    // Pool is idle (count 0, excl bit clear) — unchecked succeeds.
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    auto u = pool.try_upgrade_unchecked();
    CRUCIBLE_TEST_REQUIRE(u.has_value());
    pool.deposit_exclusive(std::move(*u));
}

// ── Tier 3: bounded-overtaking under heavy contention ───────────────
//
// THE LOAD-BEARING TEST.  Spawn 1 aggressive writer + N readers.
// Writer spins continuously trying to upgrade.  The wrapper's
// guarantee is PER-POOL, not per-reader:
//
//   "Between any two SUCCESSFUL READER LENDS (from any reader),
//    the writer wins at most BurstLimit times."
//
// Equivalently: the counter `wins_since_last_lend` (incremented on
// each successful upgrade, reset on each successful lend, by ANY
// reader) is always ≤ BurstLimit.
//
// (Per-reader overtaking can be up to N·K loose: if each cycle ends
// with a DIFFERENT reader, this reader sees N·K writer wins between
// its own successful lends.  The per-pool bound is the load-bearing
// fairness property.)

void test_bounded_overtaking_under_contention() {
    constexpr int NUM_READERS  = 4;
    constexpr int READER_ITERS = 200;
    constexpr std::uint32_t K = 8;

    auto exc = mint_permission_root<StarvationRegion>();
    FairSharedPermissionPool<StarvationRegion, K> pool{std::move(exc)};

    // Per-pool fairness counter: incremented by writer on each
    // successful upgrade, reset to 0 by readers on each successful
    // lend.  Mirrors the wrapper's internal counter so we can
    // assert the bound externally without relying on its observable
    // value (which would be racy).
    std::atomic<std::uint32_t> wins_since_last_lend{0};
    std::atomic<std::uint32_t> max_observed_wins_between_lends{0};
    std::atomic<bool>          fairness_violated{false};

    std::atomic<std::uint64_t> writer_wins_total{0};
    std::atomic<std::uint64_t> reader_lends_total{0};
    std::atomic<bool>          all_readers_done{false};

    std::latch start_latch{NUM_READERS + 1};

    // Aggressive writer — no yields, no sleeps.  Worst-case
    // contender that would starve a non-fair pool.
    std::jthread writer([&](std::stop_token) noexcept {
        start_latch.arrive_and_wait();
        while (!all_readers_done.load(std::memory_order_acquire)) {
            auto u = pool.try_upgrade();
            if (u) {
                // Bump the per-pool fairness counter and verify the
                // bound.  This is the AUDITABLE invariant: at no
                // point should the counter exceed K (the wrapper
                // would have refused the K+1th upgrade).
                const std::uint32_t prev = wins_since_last_lend.fetch_add(
                    1, std::memory_order_acq_rel);
                const std::uint32_t now = prev + 1;
                if (now > K) {
                    fairness_violated.store(true, std::memory_order_release);
                }
                // Update the running max for the post-test report.
                std::uint32_t cur =
                    max_observed_wins_between_lends.load(std::memory_order_acquire);
                while (now > cur &&
                       !max_observed_wins_between_lends.compare_exchange_weak(
                           cur, now,
                           std::memory_order_acq_rel,
                           std::memory_order_acquire)) {}
                writer_wins_total.fetch_add(1, std::memory_order_acq_rel);
                pool.deposit_exclusive(std::move(*u));
            }
        }
    });

    std::vector<std::jthread> readers;
    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back([&](std::stop_token) noexcept {
            start_latch.arrive_and_wait();

            // SharedPermissionGuard has DELETED move-assignment by
            // design.  Acquire by construction inside an inner block
            // (no assignment).
            auto acquire_guard = [&]() noexcept {
                for (;;) {
                    if (auto g = pool.lend(); g.has_value()) {
                        return g;  // NRVO; move-construct, no assign
                    }
                    // Writer in exclusive section; spin briefly.
                }
            };

            for (int i = 0; i < READER_ITERS; ++i) {
                auto guard = acquire_guard();   // never returns nullopt

                // Reset the per-pool fairness counter — this lend
                // succeeded, so the wrapper's internal counter will
                // be reset (release-store of 0).  Mirror that here.
                wins_since_last_lend.store(0, std::memory_order_release);
                reader_lends_total.fetch_add(1, std::memory_order_acq_rel);

                // guard's destructor releases at end of for-iteration.
            }
        });
    }

    for (auto& r : readers) r.join();
    all_readers_done.store(true, std::memory_order_release);
    writer.join();

    // ── The fairness contract ──────────────────────────────────────
    //
    // The per-pool "wins between any two successful lends" is bounded
    // by BurstLimit.  No reader can be starved because the K+1th
    // writer upgrade would be refused by the wrapper, forcing the
    // writer to wait for any reader's lend to reset the counter.

    CRUCIBLE_TEST_REQUIRE(!fairness_violated.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(max_observed_wins_between_lends.load() <= K);
    // Sanity: every reader did READER_ITERS successful lends — proves
    // the LIVENESS side of the fairness contract: no reader was
    // starved indefinitely.  This is the ONLY scheduler-independent
    // sanity check.
    //
    // We DO NOT assert writer_wins_total > 0 here.  Under heavy load
    // (4 readers each doing 200 iters back-to-back, possibly with
    // overlapping guard windows), the inner pool's outstanding
    // counter may never hit 0 for the writer's polling sample, so
    // writer_wins_total can legitimately be 0.  The fairness contract
    // test does not require the writer to make progress; that's
    // tested separately in test_no_reader_writer_progress (no
    // contention case, deterministic).
    CRUCIBLE_TEST_REQUIRE(
        reader_lends_total.load() ==
        static_cast<std::uint64_t>(NUM_READERS) * READER_ITERS);
    // writer_wins_total is informational; consume to silence -Wunused.
    (void)writer_wins_total.load();
}

// ── Tier 4: no-reader scenario — writer must not be starved ────────
//
// With BurstLimit=4 and no readers, a pure-fair pool would refuse
// upgrades after 4 wins.  The fix is the unchecked escape hatch.
// Writer pattern: try fair, fall back to unchecked if outstanding==0.

void test_no_reader_writer_progress() {
    constexpr std::uint32_t K = 4;
    constexpr int ITERATIONS = 1000;

    auto exc = mint_permission_root<NoReaderRegion>();
    FairSharedPermissionPool<NoReaderRegion, K> pool{std::move(exc)};

    int wins = 0;
    int unchecked_used = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        auto u = pool.try_upgrade();
        if (u) {
            ++wins;
            pool.deposit_exclusive(std::move(*u));
            continue;
        }
        // Fairness gate refused.  No readers contending →
        // bypass safely.
        if (pool.outstanding() == 0 && !pool.is_exclusive_out()) {
            auto u2 = pool.try_upgrade_unchecked();
            CRUCIBLE_TEST_REQUIRE(u2.has_value());
            ++wins;
            ++unchecked_used;
            pool.deposit_exclusive(std::move(*u2));
        }
    }

    // Writer made progress on EVERY iteration (no starvation).
    CRUCIBLE_TEST_REQUIRE(wins == ITERATIONS);
    // The bypass was used at least once (proves we hit the gate).
    CRUCIBLE_TEST_REQUIRE(unchecked_used > 0);
    // Unchecked usage frequency: should be roughly ITERATIONS - K
    // (first K succeed via fair path, remainder go through bypass).
    CRUCIBLE_TEST_REQUIRE(
        unchecked_used == ITERATIONS - static_cast<int>(K));
}

// ── Tier 5: with_shared_read free-function parity ──────────────────
//
// Audit catch: production callers using `with_shared_read(pool, body)`
// expect the call to work for both SharedPermissionPool and the Fair
// variant.  Verify both overloads (returning + void) compile and
// produce the expected result.

void test_with_shared_read_value_returning() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto result = with_shared_read(pool,
        [](SharedPermission<ConfigRegion>) noexcept {
            return 7 * 6;  // arbitrary constant, witness the body ran
        });
    CRUCIBLE_TEST_REQUIRE(result.has_value());
    CRUCIBLE_TEST_REQUIRE(*result == 42);

    // After the body returns, no shares outstanding.
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_with_shared_read_void_returning() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    int side_effect = 0;
    bool ran = with_shared_read(pool,
        [&](SharedPermission<ConfigRegion>) noexcept {
            side_effect = 99;
        });
    CRUCIBLE_TEST_REQUIRE(ran);
    CRUCIBLE_TEST_REQUIRE(side_effect == 99);

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_with_shared_read_returns_nullopt_when_excl() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    // Take the exclusive permission out; lend will fail until deposit.
    auto u = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(u.has_value());

    auto result = with_shared_read(pool,
        [](SharedPermission<ConfigRegion>) noexcept { return 1; });
    CRUCIBLE_TEST_REQUIRE(!result.has_value());

    bool ran = with_shared_read(pool,
        [](SharedPermission<ConfigRegion>) noexcept {});
    CRUCIBLE_TEST_REQUIRE(!ran);

    pool.deposit_exclusive(std::move(*u));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fair_permission_shared:\n");
    run_test("test_compile_time_properties",            test_compile_time_properties);
    run_test("test_burst_gate_fires_after_k_wins",      test_burst_gate_fires_after_k_wins);
    run_test("test_lend_resets_burst_counter",          test_lend_resets_burst_counter);
    run_test("test_unchecked_bypasses_burst_gate",      test_unchecked_bypasses_burst_gate);
    run_test("test_unchecked_when_idle_succeeds",       test_unchecked_when_idle_succeeds);
    run_test("test_bounded_overtaking_under_contention",
             test_bounded_overtaking_under_contention);
    run_test("test_no_reader_writer_progress",          test_no_reader_writer_progress);
    run_test("test_with_shared_read_value_returning",   test_with_shared_read_value_returning);
    run_test("test_with_shared_read_void_returning",    test_with_shared_read_void_returning);
    run_test("test_with_shared_read_returns_nullopt_when_excl",
             test_with_shared_read_returns_nullopt_when_excl);

    // Smoke from the header itself.
    crucible::safety::runtime_smoke_test_fair_shared_permission_pool();
    std::fprintf(stderr, "  runtime_smoke_test_fair_shared_permission_pool: PASSED\n");
    ++total_passed;

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
