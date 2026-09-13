#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {

using namespace crucible::concurrent;
using namespace crucible::safety;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

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

struct Job {
    std::uint64_t deadline_ns = 0;
    std::uint64_t payload = 0;
};

struct DeadlineKey {
    static std::uint64_t key(const Job& j) noexcept { return j.deadline_ns; }
};

struct CalTag {};

using TestGrid = PermissionedCalendarGrid<Job,
                                          /*NumProducers=*/4,
                                          /*NumBuckets=*/16,
                                          /*BucketCap=*/8, DeadlineKey,
                                          /*QuantumNs=*/1'000'000ULL,  // 1ms
                                          CalTag>;

void test_handle_sizeof_claims() {
    static_assert(sizeof(TestGrid::ProducerHandle<0>) == sizeof(TestGrid*),
                  "ProducerHandle<P> sizeof should equal Channel pointer "
                  "(Permission EBO-collapses to zero bytes)");
    static_assert(sizeof(TestGrid::ConsumerHandle) == sizeof(TestGrid*),
                  "ConsumerHandle sizeof should equal Channel pointer");

    static_assert(!std::is_same_v<TestGrid::ProducerHandle<0>, TestGrid::ProducerHandle<1>>,
                  "Producer slot index is part of the type");

    static_assert(!std::is_copy_constructible_v<TestGrid::ProducerHandle<0>>);
    static_assert(!std::is_copy_assignable_v<TestGrid::ProducerHandle<0>>);
    static_assert(std::is_move_constructible_v<TestGrid::ProducerHandle<0>>);
    static_assert(!std::is_copy_constructible_v<TestGrid::ConsumerHandle>);
    static_assert(!std::is_copy_assignable_v<TestGrid::ConsumerHandle>);
    static_assert(std::is_move_constructible_v<TestGrid::ConsumerHandle>);

    static_assert(TestGrid::is_exclusive_active() == false, "CalendarGrid is linear-only; is_exclusive_active is "
                                                            "always constexpr false");
    static_assert(TestGrid::capacity() == 4 * 16 * 8, "Channel capacity = M * NumBuckets * BucketCap");
}

void test_single_thread_priority_order() {
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    auto whole = mint_permission_root<TestGrid::whole_tag>();
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    // The deadlines arrive in reverse priority order, and the quantum
    // routes them to buckets five, three and one.
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 5'000'000, .payload = 50}));
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 3'000'000, .payload = 30}));
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 1'000'000, .payload = 10}));

    // The consumer starts at bucket zero and walks forward over the
    // empty buckets, so the items come back in priority order.
    auto pop1 = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(pop1.has_value());
    CRUCIBLE_TEST_REQUIRE(pop1->payload == 10);

    auto pop2 = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(pop2.has_value());
    CRUCIBLE_TEST_REQUIRE(pop2->payload == 30);

    auto pop3 = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(pop3.has_value());
    CRUCIBLE_TEST_REQUIRE(pop3->payload == 50);

    CRUCIBLE_TEST_REQUIRE(!cons.try_pop().has_value());
}

void test_late_item_clamps_to_current_bucket() {
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    auto whole = mint_permission_root<TestGrid::whole_tag>();
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    // Draining this item walks the current bucket past five.
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 5'000'000, .payload = 5}));
    auto first = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(first.has_value() && first->payload == 5);
    // current_bucket is now 6.

    // This deadline targets bucket two, which is now behind the current
    // bucket.  A late item clamps to the current bucket and is
    // drainable at once.
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 2'000'000, .payload = 99}));
    auto late = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(late.has_value());
    CRUCIBLE_TEST_REQUIRE(late->payload == 99);
}

void test_cross_producer_same_bucket_per_row_fifo() {
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    auto whole = mint_permission_root<TestGrid::whole_tag>();
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    // All four items land in bucket three.
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 3'000'000, .payload = 100}));
    CRUCIBLE_TEST_REQUIRE(p1.try_push(Job{.deadline_ns = 3'000'000, .payload = 200}));
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 3'000'000, .payload = 101}));
    CRUCIBLE_TEST_REQUIRE(p1.try_push(Job{.deadline_ns = 3'000'000, .payload = 201}));

    // Within one producer's row the order is first in first out.  The
    // interleave between rows follows the consumer's scan order, so only
    // the per-row order is asserted.
    std::vector<std::uint64_t> drained;
    while (auto v = cons.try_pop()) {
        drained.push_back(v->payload);
    }
    CRUCIBLE_TEST_REQUIRE(drained.size() == 4);

    auto pos100 = std::find(drained.begin(), drained.end(), 100ULL);
    auto pos101 = std::find(drained.begin(), drained.end(), 101ULL);
    auto pos200 = std::find(drained.begin(), drained.end(), 200ULL);
    auto pos201 = std::find(drained.begin(), drained.end(), 201ULL);
    CRUCIBLE_TEST_REQUIRE(pos100 != drained.end() && pos101 != drained.end());
    CRUCIBLE_TEST_REQUIRE(pos200 != drained.end() && pos201 != drained.end());
    CRUCIBLE_TEST_REQUIRE(pos100 < pos101);
    CRUCIBLE_TEST_REQUIRE(pos200 < pos201);
}

void test_batched_push_pop_priority() {
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    auto whole = mint_permission_root<TestGrid::whole_tag>();
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    // One deadline for the whole batch puts every item in bucket two,
    // which is what lets the push go through as a single batch.
    std::array<Job, 4> batch = {{
        {.deadline_ns = 2'000'000, .payload = 1},
        {.deadline_ns = 2'000'000, .payload = 2},
        {.deadline_ns = 2'000'000, .payload = 3},
        {.deadline_ns = 2'000'000, .payload = 4},
    }};
    const std::size_t pushed = p0.try_push_batch(std::span<const Job>(batch));
    CRUCIBLE_TEST_REQUIRE(pushed == 4);

    std::array<Job, 4> out{};
    const std::size_t popped = cons.try_pop_batch(std::span<Job>(out));
    CRUCIBLE_TEST_REQUIRE(popped == 4);
    CRUCIBLE_TEST_REQUIRE(out[0].payload == 1);
    CRUCIBLE_TEST_REQUIRE(out[1].payload == 2);
    CRUCIBLE_TEST_REQUIRE(out[2].payload == 3);
    CRUCIBLE_TEST_REQUIRE(out[3].payload == 4);
}

void test_multi_producer_concurrent_stress() {
    constexpr std::size_t per_producer = 1000;
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    auto whole = mint_permission_root<TestGrid::whole_tag>();
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(whole));

    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    std::atomic<bool> start{false};
    std::atomic<std::size_t> total_pushed{0};

    auto producer_body = [&](auto p_handle, std::size_t producer_id) {
        return [&, p = std::move(p_handle), producer_id](std::stop_token) mutable noexcept {
            while (!start.load(std::memory_order_acquire))
                CRUCIBLE_SPIN_PAUSE;
            for (std::size_t i = 0; i < per_producer; ++i) {
                Job j{};
                // The modulus spreads the deadlines over every bucket.
                j.deadline_ns = (i % 16) * 1'000'000;
                j.payload = producer_id * per_producer + i;
                while (!p.try_push(j))
                    CRUCIBLE_SPIN_PAUSE;
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        };
    };

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));

    std::jthread t0(producer_body(std::move(p0), 0));
    std::jthread t1(producer_body(std::move(p1), 1));
    std::jthread t2(producer_body(std::move(p2), 2));
    std::jthread t3(producer_body(std::move(p3), 3));

    constexpr std::size_t expected = 4 * per_producer;
    std::vector<bool> seen(expected, false);
    std::size_t consumed = 0;

    start.store(true, std::memory_order_release);

    while (consumed < expected) {
        if (auto v = cons.try_pop()) {
            CRUCIBLE_TEST_REQUIRE(v->payload < expected);
            CRUCIBLE_TEST_REQUIRE(!seen[v->payload]);
            seen[v->payload] = true;
            ++consumed;
        } else {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // Every payload arrived, and the check inside the drain loop already
    // rejected a repeat, so delivery was exactly once.
    for (std::size_t i = 0; i < expected; ++i) {
        CRUCIBLE_TEST_REQUIRE(seen[i]);
    }
}

void test_with_recombined_access() {
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    auto whole = mint_permission_root<TestGrid::whole_tag>();

    bool body_ran = false;
    auto returned = grid.with_recombined_access(std::move(whole), [&]() noexcept { body_ran = true; });
    CRUCIBLE_TEST_REQUIRE(body_ran);

    // The returned permission must split again.
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(returned));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));
    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 0, .payload = 42}));
    auto v = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(v.has_value() && v->payload == 42);
}

void test_diagnostic_surface() {
    auto grid_ptr = std::make_unique<TestGrid>();
    auto& grid = *grid_ptr;

    CRUCIBLE_TEST_REQUIRE(grid.empty_approx());
    CRUCIBLE_TEST_REQUIRE(grid.size_approx() == 0);
    CRUCIBLE_TEST_REQUIRE(grid.current_bucket() == 0);

    auto whole = mint_permission_root<TestGrid::whole_tag>();
    auto perms = mint_grid_permissions<TestGrid::whole_tag, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));

    CRUCIBLE_TEST_REQUIRE(p0.try_push(Job{.deadline_ns = 7'000'000, .payload = 7}));
    CRUCIBLE_TEST_REQUIRE(!grid.empty_approx());
    CRUCIBLE_TEST_REQUIRE(grid.size_approx() == 1);

    CRUCIBLE_TEST_REQUIRE(grid.is_exclusive_active() == false);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissioned_calendar_grid\n");

    run_test("test_handle_sizeof_claims", test_handle_sizeof_claims);
    run_test("test_single_thread_priority_order", test_single_thread_priority_order);
    run_test("test_late_item_clamps_to_current_bucket", test_late_item_clamps_to_current_bucket);
    run_test("test_cross_producer_same_bucket_per_row_fifo", test_cross_producer_same_bucket_per_row_fifo);
    run_test("test_batched_push_pop_priority", test_batched_push_pop_priority);
    run_test("test_multi_producer_concurrent_stress", test_multi_producer_concurrent_stress);
    run_test("test_with_recombined_access", test_with_recombined_access);
    run_test("test_diagnostic_surface", test_diagnostic_surface);

    std::fprintf(stderr, "%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
