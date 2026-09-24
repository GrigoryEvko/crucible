#include <crucible/permissions/FairSharedPermissionPool.h>

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
struct StarvationRegion {};
struct NoReaderRegion {};

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

    static_assert(FairSharedPermissionPool<ConfigRegion>::writer_burst_limit == 8);

    static_assert(std::is_same_v<P8::tag_type, ConfigRegion>);
}

void test_burst_gate_fires_after_k_wins() {
    constexpr std::uint32_t K = 5;
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, K> pool{std::move(exc)};

    CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_burst_exhausted());

    for (std::uint32_t i = 0; i < K; ++i) {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == i + 1);
        pool.deposit_exclusive(std::move(*u));
    }

    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(!u.has_value());
        // A refused attempt must leave the counter where it was.
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == K);
    }
}

void test_lend_resets_burst_counter() {
    constexpr std::uint32_t K = 4;
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, K> pool{std::move(exc)};

    for (std::uint32_t i = 0; i < K; ++i) {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        pool.deposit_exclusive(std::move(*u));
    }
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    {
        auto guard = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
    }
    CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_burst_exhausted());

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

    for (std::uint32_t i = 0; i < K; ++i) {
        auto u = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        pool.deposit_exclusive(std::move(*u));
    }
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    CRUCIBLE_TEST_REQUIRE(!pool.try_upgrade().has_value());

    {
        auto u = pool.try_upgrade_unchecked();
        CRUCIBLE_TEST_REQUIRE(u.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == K + 1);
        pool.deposit_exclusive(std::move(*u));
    }
    CRUCIBLE_TEST_REQUIRE(pool.is_burst_exhausted());

    // A lend resets the counter even after the unchecked path has pushed
    // it past the limit.
    {
        auto guard = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
    }
    CRUCIBLE_TEST_REQUIRE(pool.consecutive_writer_wins() == 0);
}

void test_unchecked_when_idle_succeeds() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion, 8> pool{std::move(exc)};

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    auto u = pool.try_upgrade_unchecked();
    CRUCIBLE_TEST_REQUIRE(u.has_value());
    pool.deposit_exclusive(std::move(*u));
}

// The fairness guarantee under test is per pool, not per reader.
// Between any two successful lends, from any reader, the writer wins at
// most BurstLimit times.  One individual reader can be overtaken up to
// readers times BurstLimit times, because each cycle may end with a
// different reader resetting the counter.
void test_bounded_overtaking_under_contention() {
    constexpr int NUM_READERS = 4;
    constexpr int READER_ITERS = 200;
    constexpr std::uint32_t K = 8;

    auto exc = mint_permission_root<StarvationRegion>();
    FairSharedPermissionPool<StarvationRegion, K> pool{std::move(exc)};

    // This counter shadows the pool's own.  Reading the pool's counter
    // from outside the critical section would race, so the writer and the
    // readers maintain a copy here and the bound is asserted on that.
    std::atomic<std::uint32_t> wins_since_last_lend{0};
    std::atomic<std::uint32_t> max_observed_wins_between_lends{0};
    std::atomic<bool> fairness_violated{false};

    std::atomic<std::uint64_t> writer_wins_total{0};
    std::atomic<std::uint64_t> reader_lends_total{0};
    std::atomic<bool> all_readers_done{false};

    std::latch start_latch{NUM_READERS + 1};

    // The writer yields nowhere and sleeps nowhere.  That is the
    // contender a pool without a burst gate would let starve the readers.
    std::jthread writer([&](std::stop_token) noexcept {
        start_latch.arrive_and_wait();
        while (!all_readers_done.load(std::memory_order_acquire)) {
            auto u = pool.try_upgrade();
            if (u) {
                // The counter can never reach K + 1, because the pool
                // refuses the upgrade that would produce it.
                const std::uint32_t prev = wins_since_last_lend.fetch_add(1, std::memory_order_acq_rel);
                const std::uint32_t now = prev + 1;
                if (now > K) {
                    fairness_violated.store(true, std::memory_order_release);
                }
                std::uint32_t cur = max_observed_wins_between_lends.load(std::memory_order_acquire);
                while (now > cur
                       && !max_observed_wins_between_lends.compare_exchange_weak(cur, now, std::memory_order_acq_rel,
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

            // The guard has no move-assignment, so it has to be acquired
            // by construction inside a block rather than assigned into an
            // existing variable.
            auto acquire_guard = [&]() noexcept {
                for (;;) {
                    if (auto g = pool.lend(); g.has_value()) {
                        return g;  // move-construct, never assign
                    }
                    // The writer holds the exclusive section.  Spin.
                }
            };

            for (int i = 0; i < READER_ITERS; ++i) {
                auto guard = acquire_guard();

                wins_since_last_lend.store(0, std::memory_order_release);
                reader_lends_total.fetch_add(1, std::memory_order_acq_rel);
            }
        });
    }

    for (auto& r : readers)
        r.join();
    all_readers_done.store(true, std::memory_order_release);
    writer.join();

    CRUCIBLE_TEST_REQUIRE(!fairness_violated.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(max_observed_wins_between_lends.load() <= K);
    // The lend count is the liveness half of the contract, and it is the
    // only claim here that does not depend on the scheduler.
    //
    // There is deliberately no assertion that the writer won at all.
    // With the readers running back to back their guard windows can
    // overlap continuously, so every sample the writer takes may find
    // the pool non-idle and it can legitimately win zero times.  Writer
    // progress is proven without contention in
    // test_no_reader_writer_progress instead.
    CRUCIBLE_TEST_REQUIRE(reader_lends_total.load() == static_cast<std::uint64_t>(NUM_READERS) * READER_ITERS);
    // Read once so the unused-variable warning stays quiet.
    (void)writer_wins_total.load();
}

// With no readers at all, nothing ever resets the counter, so a pool
// with only the gated path would refuse every upgrade after the first
// BurstLimit.  The unchecked path exists for exactly that case.
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
        // The gate refused.  With nothing outstanding there is no reader
        // to be unfair to, so the bypass is safe here.
        if (pool.outstanding() == 0 && !pool.is_exclusive_out()) {
            auto u2 = pool.try_upgrade_unchecked();
            CRUCIBLE_TEST_REQUIRE(u2.has_value());
            ++wins;
            ++unchecked_used;
            pool.deposit_exclusive(std::move(*u2));
        }
    }

    CRUCIBLE_TEST_REQUIRE(wins == ITERATIONS);
    // Without this the test could pass having never reached the gate.
    CRUCIBLE_TEST_REQUIRE(unchecked_used > 0);
    // The first K upgrades take the gated path.  Nothing resets the
    // counter afterwards, so every remaining one takes the bypass.
    CRUCIBLE_TEST_REQUIRE(unchecked_used == ITERATIONS - static_cast<int>(K));
}

// `with_shared_read` has to accept this pool as readily as the plain
// shared pool, in both the value-returning and the void form.
void test_with_shared_read_value_returning() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    auto result = with_shared_read(pool, [](SharedPermission<ConfigRegion>) noexcept {
        return 7 * 6;  // any constant will do; it witnesses the body ran
    });
    CRUCIBLE_TEST_REQUIRE(result.has_value());
    CRUCIBLE_TEST_REQUIRE(*result == 42);

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_with_shared_read_void_returning() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    int side_effect = 0;
    bool ran = with_shared_read(pool, [&](SharedPermission<ConfigRegion>) noexcept { side_effect = 99; });
    CRUCIBLE_TEST_REQUIRE(ran);
    CRUCIBLE_TEST_REQUIRE(side_effect == 99);

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
}

void test_with_shared_read_returns_nullopt_when_excl() {
    auto exc = mint_permission_root<ConfigRegion>();
    FairSharedPermissionPool<ConfigRegion> pool{std::move(exc)};

    // While the exclusive permission is out, every lend fails, and so do
    // the two calls below.
    auto u = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(u.has_value());

    auto result = with_shared_read(pool, [](SharedPermission<ConfigRegion>) noexcept { return 1; });
    CRUCIBLE_TEST_REQUIRE(!result.has_value());

    bool ran = with_shared_read(pool, [](SharedPermission<ConfigRegion>) noexcept {});
    CRUCIBLE_TEST_REQUIRE(!ran);

    pool.deposit_exclusive(std::move(*u));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fair_permission_shared:\n");
    run_test("test_compile_time_properties", test_compile_time_properties);
    run_test("test_burst_gate_fires_after_k_wins", test_burst_gate_fires_after_k_wins);
    run_test("test_lend_resets_burst_counter", test_lend_resets_burst_counter);
    run_test("test_unchecked_bypasses_burst_gate", test_unchecked_bypasses_burst_gate);
    run_test("test_unchecked_when_idle_succeeds", test_unchecked_when_idle_succeeds);
    run_test("test_bounded_overtaking_under_contention", test_bounded_overtaking_under_contention);
    run_test("test_no_reader_writer_progress", test_no_reader_writer_progress);
    run_test("test_with_shared_read_value_returning", test_with_shared_read_value_returning);
    run_test("test_with_shared_read_void_returning", test_with_shared_read_void_returning);
    run_test("test_with_shared_read_returns_nullopt_when_excl", test_with_shared_read_returns_nullopt_when_excl);

    crucible::safety::runtime_smoke_test_fair_shared_permission_pool();
    std::fprintf(stderr, "  runtime_smoke_test_fair_shared_permission_pool: PASSED\n");
    ++total_passed;

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
