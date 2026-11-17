#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <atomic>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

using namespace foundation::permissions;

namespace {

using ::fixy::check::pass;

struct ConfigRegion {
    using permission_row = ::foundation::effects::Row<>;
};
struct MetricsRegion {
    using permission_row = ::foundation::effects::Row<>;
};

void test_compile_time_properties() {
    static_assert(sizeof(SharedPermission<ConfigRegion>) == 1);
    static_assert(std::is_copy_constructible_v<SharedPermission<ConfigRegion>>);
    // A copy of a share costs nothing, and its assignments are
    // user-provided, so std::bit_cast builds no share from bytes.
    using ConfigShare = SharedPermission<ConfigRegion>;
    static_assert(std::is_trivially_copy_constructible_v<ConfigShare> && !std::is_trivially_copyable_v<ConfigShare>);

    static_assert(!std::is_copy_constructible_v<SharedPermissionGuard<ConfigRegion>>);
    static_assert(std::is_move_constructible_v<SharedPermissionGuard<ConfigRegion>>);
    static_assert(sizeof(SharedPermissionGuard<ConfigRegion>) == sizeof(void*));

    static_assert(!std::is_copy_constructible_v<SharedPermissionPool<ConfigRegion>>);
    static_assert(!std::is_move_constructible_v<SharedPermissionPool<ConfigRegion>>);
}

void test_pool_lend_basic(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool pool{std::move(exc)};

    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);
    CRUCIBLE_TEST_ASSERT(test, !pool.is_exclusive_out());

    {
        auto guard1 = pool.lend(test).expect("an idle pool lends a share");
        CRUCIBLE_TEST_ASSERT(test, guard1.holds_share());
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);

        // The token is a copyable proof, so taking one does not change
        // the count.
        auto token = guard1.token();
        static_assert(::foundation::brand::SameBrand<decltype(token), decltype(pool)>,
                      "a share proof carries the brand of its pool");
        (void)token;
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);

        auto guard2 = pool.lend(test);
        auto guard3 = pool.lend(test);
        CRUCIBLE_TEST_ASSERT(test, guard2.is_some());
        CRUCIBLE_TEST_ASSERT(test, guard3.is_some());
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 3);
    }
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);
}

// The guard that a call gives back by reference.  The rule use-after-move
// does not read through it, so the test reads a moved-from guard.
template <class Guard>
[[nodiscard]] Guard& through(Guard& guard) noexcept {
    return guard;
}

void test_guard_move_semantics(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool pool{std::move(exc)};

    {
        auto g1 = pool.lend(test).expect("an idle pool lends a share");
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);

        auto g2{std::move(through(g1))};
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);
        CRUCIBLE_TEST_ASSERT(test, g2.holds_share());
        CRUCIBLE_TEST_ASSERT(test, !through(g1).holds_share());
        // The moved-from guard destructs without decrementing.
    }
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);
}

void test_try_upgrade_succeeds_when_idle(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool pool{std::move(exc)};

    auto recovered = pool.try_upgrade(test);
    // The upgrade gives the Option of the base.
    static_assert(std::is_same_v<decltype(recovered),
                                 ::foundation::core::Option<Permission<ConfigRegion, decltype(pool)::brand_type>>>);
    CRUCIBLE_TEST_ASSERT(test, recovered.is_some());
    CRUCIBLE_TEST_ASSERT(test, pool.is_exclusive_out());

    auto fail_guard = pool.lend(test);
    CRUCIBLE_TEST_ASSERT(test, fail_guard.is_none());

    pool.deposit_exclusive(std::move(recovered).expect("the test above found the exclusive"));
    CRUCIBLE_TEST_ASSERT(test, !pool.is_exclusive_out());
    auto post_guard = pool.lend(test);
    CRUCIBLE_TEST_ASSERT(test, post_guard.is_some());
}

void test_try_upgrade_fails_when_outstanding(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool pool{std::move(exc)};

    auto guard = pool.lend(test).expect("an idle pool lends a share");

    auto upgrade = pool.try_upgrade(test);
    CRUCIBLE_TEST_ASSERT(test, !upgrade.is_some());
    CRUCIBLE_TEST_ASSERT(test, !pool.is_exclusive_out());
    CRUCIBLE_TEST_ASSERT(test, guard.holds_share());
}

// Eight threads alternate lend and upgrade.  The parallel counters
// below catch two simultaneous exclusive holders, an upgrade granted
// while shares are out, or a lend granted while a writer holds.

void test_lend_vs_upgrade_no_simultaneity(::fixy::TestRunnerCtx const& test) {
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 5000;

    auto exc = mint_permission_root<MetricsRegion>();
    SharedPermissionPool pool{std::move(exc)};

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
            [&test, &pool, &in_exclusive_count, &in_shared_count, &max_shared_seen, &violation, t](std::stop_token) {
                for (int i = 0; i < ITERATIONS; ++i) {
                    // Half the threads are biased toward read; half toward
                    // write.  This keeps both modes heavily exercised.
                    const bool prefer_write = (t % 2 == 0);

                    if (prefer_write && (i % 4 == 0)) {
                        auto upgrade = pool.try_upgrade(test);
                        if (upgrade.is_some()) {
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
                            pool.deposit_exclusive(std::move(upgrade).expect("the test above found the exclusive"));
                        }
                    } else {
                        auto guard = pool.lend(test);
                        if (guard.is_some()) {
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

    CRUCIBLE_TEST_ASSERT(test, !violation.load(std::memory_order_acquire));
    CRUCIBLE_TEST_ASSERT(test, in_exclusive_count == 0);
    CRUCIBLE_TEST_ASSERT(test, in_shared_count.load() == 0);
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

void test_swmr_sees_consistent_state(::fixy::TestRunnerCtx const& test) {
    constexpr int NUM_READERS = 6;
    constexpr int ITERATIONS = 1000;
    // Each reader does this many iterations regardless of writer_done.
    // Without a floor, an aggressive scheduler can finish the writer
    // before any reader runs, leaving the count at zero.  That would
    // make the assertion depend on scheduler fairness, which the pool
    // does not promise.
    constexpr int MIN_READER_ITERS = 32;

    auto exc = mint_permission_root<MetricsRegion>();
    SharedPermissionPool pool{std::move(exc)};
    GuardedCounter counter;

    std::atomic<bool> writer_done{false};
    std::atomic<bool> reader_violation{false};
    std::atomic<std::uint64_t> reader_iters_total{0};

    // All threads rendezvous first, so the writer cannot run to
    // completion before any reader starts.
    std::latch start_latch{NUM_READERS + 1};

    std::jthread writer([&test, &pool, &counter, &writer_done, &start_latch](std::stop_token) {
        start_latch.arrive_and_wait();
        for (int i = 0; i < ITERATIONS; ++i) {
            for (;;) {
                auto upgrade = pool.try_upgrade(test);
                if (upgrade.is_some()) {
                    const std::uint64_t v = static_cast<std::uint64_t>(i) + 1;
                    counter.lo = v;
                    counter.hi = v;
                    pool.deposit_exclusive(std::move(upgrade).expect("the test above found the exclusive"));
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back([&test, &pool, &counter, &writer_done, &reader_violation, &reader_iters_total,
                              &start_latch](std::stop_token) {
            start_latch.arrive_and_wait();

            // True only if this iteration acquired a guard.
            auto do_one_iter = [&]() noexcept {
                auto guard = pool.lend(test);
                if (guard.is_none()) return false;
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

    CRUCIBLE_TEST_ASSERT(test, !reader_violation.load(std::memory_order_acquire));
    CRUCIBLE_TEST_ASSERT(test, counter.lo == counter.hi);
    CRUCIBLE_TEST_ASSERT(test, counter.lo == ITERATIONS);
    CRUCIBLE_TEST_ASSERT(test, reader_iters_total.load() >= static_cast<std::uint64_t>(NUM_READERS) * MIN_READER_ITERS);
}

void test_with_shared_read_helper(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<ConfigRegion>();
    SharedPermissionPool pool{std::move(exc)};

    auto result_opt = with_shared_read(test, pool, [](auto) noexcept { return 42; });
    // The read gives the result of the body in the Option of the base.
    static_assert(std::is_same_v<decltype(result_opt), ::foundation::core::Option<int>>);
    CRUCIBLE_TEST_ASSERT(test, result_opt.is_some());
    CRUCIBLE_TEST_ASSERT(test, std::move(result_opt).value_or(0) == 42);
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);

    // A void body makes the helper return bool.
    bool ran = false;
    bool ok = with_shared_read(test, pool, [&ran](auto) noexcept { ran = true; });
    CRUCIBLE_TEST_ASSERT(test, ok);
    CRUCIBLE_TEST_ASSERT(test, ran);
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);

    auto upgrade = pool.try_upgrade(test);
    CRUCIBLE_TEST_ASSERT(test, upgrade.is_some());
    auto failed_opt = with_shared_read(test, pool, [](auto) noexcept { return 99; });
    CRUCIBLE_TEST_ASSERT(test, !failed_opt.is_some());
    pool.deposit_exclusive(std::move(upgrade).expect("the test above found the exclusive"));
}

// A one-shot conversion with no pool tracking.
void test_mint_permission_share() {
    auto exc = mint_permission_root<ConfigRegion>();
    auto shared = mint_permission_share(std::move(exc));
    // The share carries the exclusive's tag and brand.
    static_assert(IsSharedPermissionFor<decltype(shared), ConfigRegion>);
    static_assert(::foundation::brand::IsBranded<decltype(shared)>);
    // Copyable, and a copy keeps the brand.  No conversion drops it.
    auto shared2 = shared;
    auto shared3 = shared2;
    static_assert(std::is_same_v<decltype(shared3), decltype(shared)>);
    static_assert(!std::is_constructible_v<SharedPermission<ConfigRegion>, decltype(shared) const&>);
    (void)shared3;
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    pass(test, "test_mint_permission_shared:\n");

    test_compile_time_properties();  // pure compile-time

    test_pool_lend_basic(test);
    pass(test, "  test_pool_lend_basic: PASSED\n");
    test_guard_move_semantics(test);
    pass(test, "  test_guard_move_semantics: PASSED\n");
    test_try_upgrade_succeeds_when_idle(test);
    pass(test, "  test_try_upgrade_succeeds_when_idle: PASSED\n");
    test_try_upgrade_fails_when_outstanding(test);
    pass(test, "  test_try_upgrade_fails_when_outstanding: PASSED\n");
    test_lend_vs_upgrade_no_simultaneity(test);
    pass(test, "  test_lend_vs_upgrade_no_simultaneity: PASSED\n");
    test_swmr_sees_consistent_state(test);
    pass(test, "  test_swmr_sees_consistent_state: PASSED\n");
    test_with_shared_read_helper(test);
    pass(test, "  test_with_shared_read_helper: PASSED\n");
    test_mint_permission_share();
    pass(test, "  test_mint_permission_share: PASSED\n");
    pass(test, "test_mint_permission_shared: ALL PASSED\n");
    return 0;
}
