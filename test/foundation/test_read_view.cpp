#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/permissions/ReadView.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <utility>

using namespace foundation::permissions;

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

// Every tag declares its row: the relation from tag to row is closed,
// and a pure tag says so with Row<>.
struct ConfigData {
    using permission_row = ::foundation::effects::Row<>;
};
struct LoopBounds {
    using permission_row = ::foundation::effects::Row<>;
};
struct WorkerSlice {
    using permission_row = ::foundation::effects::Row<>;
};

void test_compile_time_properties() {
    // One byte, which is the minimum an empty class can occupy on its own.
    static_assert(sizeof(ReadView<ConfigData>) == 1);
    static_assert(sizeof(ReadLoan<ConfigData>) == 1);
    static_assert(sizeof(LentPermission<ConfigData>) == 1);

    static_assert(!std::is_trivially_copyable_v<ReadView<ConfigData>>);
    static_assert(!std::is_implicit_lifetime_v<ReadView<ConfigData>>);
    static_assert(!std::is_default_constructible_v<ReadView<ConfigData>>);
    static_assert(!std::is_copy_assignable_v<ReadView<ConfigData>>);
    static_assert(!std::is_move_assignable_v<ReadView<ConfigData>>);

    // A view lives in the frame of its door: no copy and no move takes
    // it out of the frame.
    static_assert(!std::is_copy_constructible_v<ReadView<ConfigData>>);
    static_assert(!std::is_move_constructible_v<ReadView<ConfigData>>);

    // A loan moves and never copies, so one parked token has one loan.
    static_assert(std::is_move_constructible_v<ReadLoan<ConfigData>>);
    static_assert(!std::is_copy_constructible_v<ReadLoan<ConfigData>>);
    static_assert(!std::is_copy_constructible_v<LentPermission<ConfigData>>);

    using V1 = ReadView<ConfigData>;
    using V2 = ReadView<LoopBounds>;
    static_assert(!std::is_same_v<V1, V2>, "Distinct tags must produce distinct ReadView types");
}

// A body that returns nothing gives the source back.
void test_door_void_body_hands_the_source_back() {
    auto perm = mint_permission_root<ConfigData>();
    bool ran = false;

    auto back = with_read_view(std::move(perm), [&ran](ReadView<ConfigData> const&) noexcept { ran = true; });

    CRUCIBLE_TEST_REQUIRE(ran);
    static_assert(std::is_same_v<decltype(back), decltype(perm)>, "the source comes back with its own brand");
    permission_drop(std::move(back));
}

// A body that returns a value gives the value and the source.
void test_door_value_body() {
    auto perm = mint_permission_root<LoopBounds>();
    constexpr int sentinel = 12345;

    auto [observed, back] =
        with_read_view(std::move(perm), [](ReadView<LoopBounds> const&) noexcept { return sentinel; });

    CRUCIBLE_TEST_REQUIRE(observed == sentinel);
    permission_drop(std::move(back));
}

void test_door_struct_body() {
    struct Result {
        std::uint64_t lo;
        std::uint64_t hi;
    };

    auto perm = mint_permission_root<LoopBounds>();
    auto [result, back] =
        with_read_view(std::move(perm), [](ReadView<LoopBounds> const&) noexcept { return Result{42, 99}; });

    CRUCIBLE_TEST_REQUIRE(result.lo == 42);
    CRUCIBLE_TEST_REQUIRE(result.hi == 99);
    permission_drop(std::move(back));
}

// A generic body sees the branded view.  A body that names the erased
// view gets the erased view.
void test_door_picks_the_view_the_body_asks_for() {
    auto perm = mint_permission_root<ConfigData>();
    auto [branded, again] = with_read_view(std::move(perm), [](auto const& view) noexcept {
        return ::foundation::brand::IsBranded<std::remove_cvref_t<decltype(view)>>;
    });
    auto [erased, back] = with_read_view(std::move(again), [](ReadView<ConfigData> const& view) noexcept {
        return ::foundation::brand::IsErased<std::remove_cvref_t<decltype(view)>>;
    });
    CRUCIBLE_TEST_REQUIRE(branded);
    CRUCIBLE_TEST_REQUIRE(erased);
    permission_drop(std::move(back));
}

namespace fork_tags {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace fork_tags

}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<fork_tags::Whole, fork_tags::Left, fork_tags::Right> : std::true_type {};

template <>
struct has_split_pack_authoring_witness<fork_tags::Whole, fork_tags::Left, fork_tags::Right> : std::true_type {};
}  // namespace foundation::permissions

namespace {

namespace eff = ::foundation::effects;

// The spawning arm wants a context that owns the background effect; the
// inline arm runs in any context, here the foreground one.
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using HotFgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

// Two writers, each with its own write permission, read one view of the
// shared configuration.  The fork joins before the body of the door
// returns, so each child holds a reference to a view that outlives it.
void test_fork_inside_the_door() {
    auto config_perm = mint_permission_root<ConfigData>();

    std::atomic<int> left_done{0};
    std::atomic<int> right_done{0};

    auto whole = mint_permission_root<fork_tags::Whole>();

    auto [rebuilt, config_back] = with_read_view(std::move(config_perm), [&](ReadView<ConfigData> const& cv) {
        return mint_permission_fork<fork_tags::Left, fork_tags::Right>(
            BgDrainCtx{::foundation::effects::testing::bg()}, std::move(whole),
            [&cv, &left_done](Permission<fork_tags::Left>, BgDrainCtx const&) noexcept {
                (void)cv;
                left_done.store(1, std::memory_order_release);
            },
            [&cv, &right_done](Permission<fork_tags::Right>, BgDrainCtx const&) noexcept {
                (void)cv;
                right_done.store(1, std::memory_order_release);
            });
    });

    // The fork has returned, so both workers have joined.
    CRUCIBLE_TEST_REQUIRE(left_done.load() == 1);
    CRUCIBLE_TEST_REQUIRE(right_done.load() == 1);

    permission_drop(std::move(rebuilt));
    permission_drop(std::move(config_back));
}

// The same fork on the inline arm: the bodies run in child order on
// this thread, and the foreground context needs no background effect.
void test_fork_inline_inside_the_door() {
    auto config_perm = mint_permission_root<ConfigData>();

    int order = 0;
    int left_seen_at = 0;
    int right_seen_at = 0;

    auto whole = mint_permission_root<fork_tags::Whole>();

    auto [rebuilt, config_back] = with_read_view(std::move(config_perm), [&](ReadView<ConfigData> const& cv) {
        return mint_permission_fork_inline<fork_tags::Left, fork_tags::Right>(
            HotFgCtx{}, std::move(whole),
            [&cv, &order, &left_seen_at](Permission<fork_tags::Left>, HotFgCtx const&) noexcept {
                (void)cv;
                left_seen_at = ++order;
            },
            [&cv, &order, &right_seen_at](Permission<fork_tags::Right>, HotFgCtx const&) noexcept {
                (void)cv;
                right_seen_at = ++order;
            });
    });

    CRUCIBLE_TEST_REQUIRE(left_seen_at == 1);
    CRUCIBLE_TEST_REQUIRE(right_seen_at == 2);

    permission_drop(std::move(rebuilt));
    permission_drop(std::move(config_back));
}

// A view from a live share.  The door holds the guard while the body
// runs and hands it back, so the share stays counted throughout, and
// the pool cannot upgrade until the guard ends.
void test_door_over_a_share_guard() {
    SharedPermissionPool pool{mint_permission_root<ConfigData>()};
    {
        auto guard = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
        auto [counted, held] = with_read_view(std::move(*guard), [&pool](auto const& view) noexcept {
            static_assert(std::is_same_v<typename std::remove_cvref_t<decltype(view)>::tag_type, ConfigData>);
            return pool.outstanding();
        });
        CRUCIBLE_TEST_REQUIRE(counted == 1);
        CRUCIBLE_TEST_REQUIRE(held.holds_share());
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);
        CRUCIBLE_TEST_REQUIRE(!pool.try_upgrade().has_value());
    }
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
    auto exclusive = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(exclusive.has_value());
    permission_drop(std::move(*exclusive));
}

// A loan travels to a reader on another thread.  The lender holds a
// LentPermission, which gives no access, until the loan comes back.
void test_loan_across_threads() {
    auto token = mint_permission_root<ConfigData>();
    auto [loan, lent] = mint_read_loan(std::move(token));

    std::atomic<int> reads{0};
    ReadLoan<ConfigData, decltype(token)::brand_type> returned = [&] {
        auto reader_loan = std::move(loan);
        std::jthread reader{[&reads, &reader_loan] {
            for (int round = 0; round < 3; ++round) {
                reader_loan = with_read_view(std::move(reader_loan), [&reads](auto const&) noexcept {
                    reads.fetch_add(1, std::memory_order_relaxed);
                });
            }
        }};
        reader.join();
        return reader_loan;
    }();

    CRUCIBLE_TEST_REQUIRE(reads.load() == 3);
    auto back = mint_permission_after_loan(std::move(lent), std::move(returned));
    static_assert(std::is_same_v<decltype(back), decltype(token)>, "the token comes back with its own brand");
    permission_drop(std::move(back));
}

// A handle keeps a write token and a read loan.  Both are empty and
// collapse, so the handle carries no payload.
struct WorkerHandle {
    [[no_unique_address]] Permission<WorkerSlice> write_perm;
    [[no_unique_address]] ReadLoan<ConfigData> config_loan;

    constexpr WorkerHandle(Permission<WorkerSlice>&& wp, ReadLoan<ConfigData>&& loan) noexcept
        : write_perm{std::move(wp)}, config_loan{std::move(loan)} {}
};

void test_handle_composition_zero_cost() {
    static_assert(sizeof(WorkerHandle) <= 2, "a handle of two empty proof tokens must be at most 2 bytes");

    auto [loan, lent] = mint_read_loan(Permission<ConfigData>{mint_permission_root<ConfigData>()});
    WorkerHandle handle{Permission<WorkerSlice>{mint_permission_root<WorkerSlice>()}, std::move(loan)};
    auto config = mint_permission_after_loan(std::move(lent), std::move(handle.config_loan));
    permission_drop(std::move(config));
    permission_drop(std::move(handle.write_perm));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_read_view:\n");

    // Nothing to observe at runtime, so this one is called directly.
    test_compile_time_properties();

    run_test("test_door_void_body_hands_the_source_back", test_door_void_body_hands_the_source_back);
    run_test("test_door_value_body", test_door_value_body);
    run_test("test_door_struct_body", test_door_struct_body);
    run_test("test_door_picks_the_view_the_body_asks_for", test_door_picks_the_view_the_body_asks_for);
    run_test("test_fork_inside_the_door", test_fork_inside_the_door);
    run_test("test_fork_inline_inside_the_door", test_fork_inline_inside_the_door);
    run_test("test_door_over_a_share_guard", test_door_over_a_share_guard);
    run_test("test_loan_across_threads", test_loan_across_threads);
    run_test("test_handle_composition_zero_cost", test_handle_composition_zero_cost);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
