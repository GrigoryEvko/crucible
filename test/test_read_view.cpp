// ═══════════════════════════════════════════════════════════════════
// test_read_view — ReadView<Tag> lifetime-bound borrow (SEPLOG-A3)
//
// Coverage:
//   Tier 1: structural — sizeof, copy/move semantics, EBO collapse
//   Tier 2: factory — lend_read produces ReadView; multiple coexist
//   Tier 3: with_read_view scoped helper (void + non-void return)
//   Tier 4: composition — ReadView as member via [[no_unique_address]]
//   Tier 5: composition with permission_fork — read-only side info
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Permission.h>
#include <crucible/safety/PermissionFork.h>
#include <crucible/safety/ReadView.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

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
struct ConfigData {};
struct LoopBounds {};
struct WorkerSlice {};
struct WorkerSlice0 {};
struct WorkerSlice1 {};

// ── Tier 1: compile-time structural ──────────────────────────────

void test_compile_time_properties() {
    // sizeof = 1 (empty class minimum).
    static_assert(sizeof(ReadView<ConfigData>) == 1);

    // Trivially copyable + destructible.
    static_assert(std::is_trivially_copyable_v<ReadView<ConfigData>>);
    static_assert(std::is_trivially_destructible_v<ReadView<ConfigData>>);

    // Copyable but not assignable (single-binding).
    static_assert(std::is_copy_constructible_v<ReadView<ConfigData>>);
    static_assert(std::is_move_constructible_v<ReadView<ConfigData>>);
    static_assert(!std::is_copy_assignable_v<ReadView<ConfigData>>);
    static_assert(!std::is_move_assignable_v<ReadView<ConfigData>>);

    // Tag identity preserved.
    using V1 = ReadView<ConfigData>;
    using V2 = ReadView<LoopBounds>;
    static_assert(!std::is_same_v<V1, V2>,
                  "Distinct tags must produce distinct ReadView types");
}

// EBO collapse: a struct embedding ReadView via [[no_unique_address]]
// should not pay any byte for the view.
struct EboHost {
    void* p = nullptr;
    [[no_unique_address]] ReadView<ConfigData> view{};
};
static_assert(sizeof(EboHost) == sizeof(void*),
              "ReadView via [[no_unique_address]] must collapse to 0 bytes");

// ── Tier 2: lend_read factory ────────────────────────────────────

void test_lend_read_basic() {
    auto perm = permission_root_mint<ConfigData>();
    auto view = lend_read(perm);

    // ReadView is an empty proof token; nothing observable to check
    // beyond compile-time type identity.
    static_assert(std::is_same_v<decltype(view), ReadView<ConfigData>>);
}

void test_multiple_views_coexist() {
    auto perm = permission_root_mint<ConfigData>();
    auto v1 = lend_read(perm);
    auto v2 = lend_read(perm);
    auto v3 = lend_read(perm);

    // All three coexist; this is the point of read views.  No
    // refcount, no allocation, no atomic.  Type-system-only.
    (void)v1; (void)v2; (void)v3;

    // Even copies of copies work.
    auto v4 = v1;
    auto v5 = v4;
    (void)v5;
}

// ── Tier 3: with_read_view scoped helper ─────────────────────────

void test_with_read_view_void_body() {
    auto perm = permission_root_mint<ConfigData>();
    bool ran = false;

    with_read_view(perm, [&ran](ReadView<ConfigData>) noexcept {
        ran = true;
    });

    CRUCIBLE_TEST_REQUIRE(ran);
}

void test_with_read_view_returning_value() {
    auto perm = permission_root_mint<LoopBounds>();
    constexpr int sentinel = 12345;

    int observed = with_read_view(perm,
        [](ReadView<LoopBounds>) noexcept { return sentinel; });

    CRUCIBLE_TEST_REQUIRE(observed == sentinel);
}

void test_with_read_view_returning_struct() {
    struct Result {
        std::uint64_t lo;
        std::uint64_t hi;
    };

    auto perm = permission_root_mint<LoopBounds>();
    auto r = with_read_view(perm,
        [](ReadView<LoopBounds>) noexcept {
            return Result{42, 99};
        });

    CRUCIBLE_TEST_REQUIRE(r.lo == 42);
    CRUCIBLE_TEST_REQUIRE(r.hi == 99);
}

// ── Tier 4: composition — ReadView as struct member ──────────────

// Worker handle that wants exclusive write on its slice + read-only
// access to a shared config.  The ReadView lives as long as the
// handle (per [[no_unique_address]] field) but does not consume any
// bytes.
struct WorkerHandle {
    [[no_unique_address]] Permission<WorkerSlice> write_perm;
    [[no_unique_address]] ReadView<ConfigData>     config_view;

    constexpr explicit WorkerHandle(
        Permission<WorkerSlice>&& wp,
        ReadView<ConfigData>     cv) noexcept
        : write_perm{std::move(wp)}, config_view{cv} {}
};

void test_handle_composition_zero_cost() {
    // Both Permission AND ReadView are 1-byte empty classes; via EBO
    // they collapse and the handle has no payload.  But the handle
    // itself is a class — it has at least 1 byte (empty-class minimum).
    static_assert(sizeof(WorkerHandle) <= 2,
                  "WorkerHandle composing two empty proof tokens must be at most 2 bytes");

    auto config_perm = permission_root_mint<ConfigData>();
    auto worker_perm = permission_root_mint<WorkerSlice>();
    auto cv = lend_read(config_perm);

    WorkerHandle h{std::move(worker_perm), cv};
    (void)h;
    // Handle owns the write permission; read view is a borrow.
}

// ── Tier 5: integration with permission_fork ─────────────────────

namespace fork_tags {
    struct Whole  {};
    struct Left   {};
    struct Right  {};
}

}  // namespace

namespace crucible::safety {
template <>
struct splits_into_pack<fork_tags::Whole, fork_tags::Left, fork_tags::Right>
    : std::true_type {};
}  // namespace crucible::safety

namespace {

// permission_fork into two writers, each receiving a separate
// Permission AND a read-only view of the shared config.
void test_fork_with_shared_read_view() {
    auto config_perm = permission_root_mint<ConfigData>();
    const auto cv = lend_read(config_perm);  // copyable; safe to share

    std::atomic<int> left_done{0};
    std::atomic<int> right_done{0};

    auto whole = permission_root_mint<fork_tags::Whole>();
    auto rebuilt = permission_fork<fork_tags::Left, fork_tags::Right>(
        std::move(whole),
        [cv, &left_done](Permission<fork_tags::Left>) noexcept {
            // Worker sees read view of config; type system confirms
            // it can only READ (no methods to mutate).
            (void)cv;
            left_done.store(1, std::memory_order_release);
        },
        [cv, &right_done](Permission<fork_tags::Right>) noexcept {
            (void)cv;
            right_done.store(1, std::memory_order_release);
        }
    );

    // After fork returns, both workers joined.  Both saw the read view.
    CRUCIBLE_TEST_REQUIRE(left_done.load() == 1);
    CRUCIBLE_TEST_REQUIRE(right_done.load() == 1);

    // Original Permissions still alive in this scope.
    crucible::safety::permission_drop(std::move(rebuilt));
    (void)config_perm;  // outlived the fork; cv was bound to it
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_read_view:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_lend_read_basic",            test_lend_read_basic);
    run_test("test_multiple_views_coexist",     test_multiple_views_coexist);
    run_test("test_with_read_view_void_body",   test_with_read_view_void_body);
    run_test("test_with_read_view_returning_value",
             test_with_read_view_returning_value);
    run_test("test_with_read_view_returning_struct",
             test_with_read_view_returning_struct);
    run_test("test_handle_composition_zero_cost",
             test_handle_composition_zero_cost);
    run_test("test_fork_with_shared_read_view",
             test_fork_with_shared_read_view);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
