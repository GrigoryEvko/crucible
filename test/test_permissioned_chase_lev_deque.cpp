// A work-stealing deque with one owner and many thieves.  The owner holds
// an exclusive permission and works the bottom of the queue.  Each thief
// holds a share and steals from the top.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
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

struct PoolDeque {};
struct DrainDeque {};

using IntDeque = PermissionedChaseLevDeque<int, 256, PoolDeque>;
using OwnerH = IntDeque::OwnerHandle;
using ThiefH = IntDeque::ThiefHandle;

static_assert(!std::is_copy_constructible_v<OwnerH>);
static_assert(std::is_move_constructible_v<OwnerH>);
static_assert(!std::is_copy_constructible_v<ThiefH>);
static_assert(std::is_move_constructible_v<ThiefH>);
static_assert(!std::is_default_constructible_v<OwnerH>);
static_assert(!std::is_default_constructible_v<ThiefH>);
static_assert(!std::is_move_assignable_v<OwnerH>);
static_assert(!std::is_move_assignable_v<ThiefH>);

void test_single_thread_owner_round_trip() {
    PermissionedChaseLevDeque<int, 64, PoolDeque> deque;
    auto owner_perm = mint_permission_root<deque_tag::Owner<PoolDeque>>();
    auto owner = deque.owner(std::move(owner_perm));

    for (int i = 0; i < 5; ++i) {
        CRUCIBLE_TEST_REQUIRE(owner.try_push(i));
    }

    // The owner's end of the queue is last in, first out, so the values
    // come back in reverse.
    int expected = 4;
    int popped = 0;
    while (auto v = owner.try_pop()) {
        CRUCIBLE_TEST_REQUIRE(*v == expected);
        --expected;
        ++popped;
    }
    CRUCIBLE_TEST_REQUIRE(popped == 5);
}

void test_owner_pushes_thieves_steal() {
    constexpr int N_THIEVES = 4;
    constexpr int N_PUSHES = 5000;

    PermissionedChaseLevDeque<int, 1024, PoolDeque> deque;
    auto owner_perm = mint_permission_root<deque_tag::Owner<PoolDeque>>();

    std::atomic<int> total_handled{0};
    std::atomic<bool> owner_done{false};
    std::vector<std::jthread> thieves;
    thieves.reserve(N_THIEVES);

    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&deque, &total_handled, &owner_done] {
            auto t_opt = deque.thief();
            if (!t_opt) return;
            auto thief = std::move(*t_opt);
            for (;;) {
                if (auto v = thief.try_steal()) {
                    (void)v;
                    total_handled.fetch_add(1, std::memory_order_relaxed);
                } else {
                    if (owner_done.load(std::memory_order_acquire) && thief.empty_approx()) {
                        return;
                    }
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        });
    }

    {
        auto owner = deque.owner(std::move(owner_perm));
        for (int i = 0; i < N_PUSHES; ++i) {
            while (!owner.try_push(i))
                CRUCIBLE_SPIN_PAUSE;
        }
        // The owner works the bottom while the thieves work the top.
        while (auto v = owner.try_pop()) {
            (void)v;
            total_handled.fetch_add(1, std::memory_order_relaxed);
        }
    }
    owner_done.store(true, std::memory_order_release);

    for (auto& t : thieves)
        t.join();

    // Every push is seen exactly once, whether by the owner or a thief.
    CRUCIBLE_TEST_REQUIRE(total_handled.load() == N_PUSHES);
}

void test_drained_thieves_refuses_while_thief_out() {
    PermissionedChaseLevDeque<int, 64, DrainDeque> deque;

    auto t_opt = deque.thief();
    CRUCIBLE_TEST_REQUIRE(t_opt.has_value());

    bool ran = false;
    bool ok = deque.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!ran);

    // With the share dropped it must succeed.
    t_opt.reset();
    ok = deque.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);

    // The deque returns to lending afterwards.
    auto t2 = deque.thief();
    CRUCIBLE_TEST_REQUIRE(t2.has_value());
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_chase_lev_deque]\n");
    run_test("single_thread_owner_round_trip", test_single_thread_owner_round_trip);
    run_test("owner_pushes_thieves_steal", test_owner_pushes_thieves_steal);
    run_test("drained_thieves_refuses_while_thief_out", test_drained_thieves_refuses_while_thief_out);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
