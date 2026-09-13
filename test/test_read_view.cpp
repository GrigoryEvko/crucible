#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/ReadView.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

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

struct ConfigData {};
struct LoopBounds {};
struct WorkerSlice {};
struct WorkerSlice0 {};
struct WorkerSlice1 {};

void test_compile_time_properties() {
    // One byte, which is the minimum an empty class can occupy on its own.
    static_assert(sizeof(ReadView<ConfigData>) == 1);

    static_assert(std::is_trivially_copyable_v<ReadView<ConfigData>>);
    static_assert(std::is_trivially_destructible_v<ReadView<ConfigData>>);

    // Copyable but not assignable, because a view binds once.
    static_assert(std::is_copy_constructible_v<ReadView<ConfigData>>);
    static_assert(std::is_move_constructible_v<ReadView<ConfigData>>);
    static_assert(!std::is_copy_assignable_v<ReadView<ConfigData>>);
    static_assert(!std::is_move_assignable_v<ReadView<ConfigData>>);

    using V1 = ReadView<ConfigData>;
    using V2 = ReadView<LoopBounds>;
    static_assert(!std::is_same_v<V1, V2>, "Distinct tags must produce distinct ReadView types");
}

// The view is passed in rather than default-constructed, because the default
// constructor is private. A view cannot be conjured from nothing.
struct EboHost {
    void* p = nullptr;
    [[no_unique_address]] ReadView<ConfigData> view;

    constexpr explicit EboHost(ReadView<ConfigData> v) noexcept : view{v} {}
};
static_assert(sizeof(EboHost) == sizeof(void*), "ReadView via [[no_unique_address]] must collapse to 0 bytes");

void test_mint_read_view_basic() {
    auto perm = mint_permission_root<ConfigData>();
    auto view = mint_read_view(perm);

    // A view carries no state, so its type is the only thing to check.
    static_assert(std::is_same_v<decltype(view), ReadView<ConfigData>>);
}

void test_multiple_views_coexist() {
    auto perm = mint_permission_root<ConfigData>();
    auto v1 = mint_read_view(perm);
    auto v2 = mint_read_view(perm);
    auto v3 = mint_read_view(perm);

    // Coexisting is the point. There is no refcount, no allocation and no
    // atomic behind them.
    (void)v1;
    (void)v2;
    (void)v3;

    auto v4 = v1;
    auto v5 = v4;
    (void)v5;
}

void test_with_read_view_void_body() {
    auto perm = mint_permission_root<ConfigData>();
    bool ran = false;

    with_read_view(perm, [&ran](ReadView<ConfigData>) noexcept { ran = true; });

    CRUCIBLE_TEST_REQUIRE(ran);
}

void test_with_read_view_returning_value() {
    auto perm = mint_permission_root<LoopBounds>();
    constexpr int sentinel = 12345;

    int observed = with_read_view(perm, [](ReadView<LoopBounds>) noexcept { return sentinel; });

    CRUCIBLE_TEST_REQUIRE(observed == sentinel);
}

void test_with_read_view_returning_struct() {
    struct Result {
        std::uint64_t lo;
        std::uint64_t hi;
    };

    auto perm = mint_permission_root<LoopBounds>();
    auto r = with_read_view(perm, [](ReadView<LoopBounds>) noexcept { return Result{42, 99}; });

    CRUCIBLE_TEST_REQUIRE(r.lo == 42);
    CRUCIBLE_TEST_REQUIRE(r.hi == 99);
}

// Exclusive write on one slice alongside read-only access to shared
// configuration. The view lives as long as the handle and costs it nothing.
struct WorkerHandle {
    [[no_unique_address]] Permission<WorkerSlice> write_perm;
    [[no_unique_address]] ReadView<ConfigData> config_view;

    constexpr explicit WorkerHandle(Permission<WorkerSlice>&& wp, ReadView<ConfigData> cv) noexcept
        : write_perm{std::move(wp)}, config_view{cv} {}
};

void test_handle_composition_zero_cost() {
    // Both members are empty and collapse, so the handle carries no payload.
    // The bound is two rather than one because the handle is itself a class
    // and cannot be smaller than a byte.
    static_assert(sizeof(WorkerHandle) <= 2, "WorkerHandle composing two empty proof tokens must be at most 2 bytes");

    auto config_perm = mint_permission_root<ConfigData>();
    auto worker_perm = mint_permission_root<WorkerSlice>();
    auto cv = mint_read_view(config_perm);

    WorkerHandle h{std::move(worker_perm), cv};
    (void)h;
}

namespace fork_tags {
struct Whole {};
struct Left {};
struct Right {};
}  // namespace fork_tags

}  // namespace

namespace crucible::safety {
template <>
struct splits_into_pack<fork_tags::Whole, fork_tags::Left, fork_tags::Right> : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<fork_tags::Whole, fork_tags::Left, fork_tags::Right> : std::true_type {};
}  // namespace crucible::safety

namespace {

// Two writers, each with its own write permission and both with the same
// read-only view of the shared configuration.
void test_fork_with_shared_read_view() {
    auto config_perm = mint_permission_root<ConfigData>();
    // A view is copyable, so both workers can capture it by value.
    const auto cv = mint_read_view(config_perm);

    std::atomic<int> left_done{0};
    std::atomic<int> right_done{0};

    auto whole = mint_permission_root<fork_tags::Whole>();
    const auto fork_decision = ::crucible::concurrent::parallelism_decision_for<::crucible::effects::BgDrainCtx>();
    CRUCIBLE_TEST_REQUIRE(fork_decision.kind == ::crucible::concurrent::ParallelismDecision::Kind::Sequential);

    auto rebuilt = mint_permission_fork<fork_tags::Left, fork_tags::Right>(
        ::crucible::effects::BgDrainCtx{}, std::move(whole),
        [cv, &left_done](Permission<fork_tags::Left>, ::crucible::effects::BgDrainCtx const&) noexcept {
            // The view exposes no mutating operation at all, so read-only
            // is a property of the type rather than of the body.
            (void)cv;
            left_done.store(1, std::memory_order_release);
        },
        [cv, &right_done](Permission<fork_tags::Right>, ::crucible::effects::BgDrainCtx const&) noexcept {
            (void)cv;
            right_done.store(1, std::memory_order_release);
        });

    // The fork has returned, so both workers have joined.
    CRUCIBLE_TEST_REQUIRE(left_done.load() == 1);
    CRUCIBLE_TEST_REQUIRE(right_done.load() == 1);

    crucible::safety::permission_drop(std::move(rebuilt));
    // The permission that mints the view outlives the fork, which is what
    // keeps the view valid for its whole lifetime.
    (void)config_perm;
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_read_view:\n");

    // Nothing to observe at runtime, so this one is called directly.
    test_compile_time_properties();

    run_test("test_mint_read_view_basic", test_mint_read_view_basic);
    run_test("test_multiple_views_coexist", test_multiple_views_coexist);
    run_test("test_with_read_view_void_body", test_with_read_view_void_body);
    run_test("test_with_read_view_returning_value", test_with_read_view_returning_value);
    run_test("test_with_read_view_returning_struct", test_with_read_view_returning_struct);
    run_test("test_handle_composition_zero_cost", test_handle_composition_zero_cost);
    run_test("test_fork_with_shared_read_view", test_fork_with_shared_read_view);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
