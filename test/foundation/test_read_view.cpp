#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/permissions/SharedPermission.h>

#include <cstdint>
#include <type_traits>
#include <utility>

using namespace foundation::permissions;

// The tag of a singleton root has a stable identity, so it has external
// linkage.
namespace test_read_view_singleton {
struct ErasedData {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace test_read_view_singleton

namespace {

using ::fixy::check::pass;

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
void test_door_void_body_hands_the_source_back(::fixy::TestRunnerCtx const& test) {
    auto perm = mint_permission_root<ConfigData>();
    using Brand = decltype(perm)::brand_type;
    bool ran = false;

    auto back = with_read_view(std::move(perm), [&ran](ReadView<ConfigData, Brand> const&) noexcept { ran = true; });

    CRUCIBLE_TEST_ASSERT(test, ran);
    static_assert(std::is_same_v<decltype(back), decltype(perm)>, "the source comes back with its own brand");
    ::foundation::linear::drop(std::move(back));
}

// A body that returns a value gives the value and the source.
void test_door_value_body(::fixy::TestRunnerCtx const& test) {
    auto perm = mint_permission_root<LoopBounds>();
    using Brand = decltype(perm)::brand_type;
    constexpr int sentinel = 12345;

    auto [observed, back] =
        with_read_view(std::move(perm), [](ReadView<LoopBounds, Brand> const&) noexcept { return sentinel; });

    CRUCIBLE_TEST_ASSERT(test, observed == sentinel);
    ::foundation::linear::drop(std::move(back));
}

void test_door_struct_body(::fixy::TestRunnerCtx const& test) {
    struct Result {
        std::uint64_t lo;
        std::uint64_t hi;
    };

    auto perm = mint_permission_root<LoopBounds>();
    using Brand = decltype(perm)::brand_type;
    auto [result, back] =
        with_read_view(std::move(perm), [](ReadView<LoopBounds, Brand> const&) noexcept { return Result{42, 99}; });

    CRUCIBLE_TEST_ASSERT(test, result.lo == 42);
    CRUCIBLE_TEST_ASSERT(test, result.hi == 99);
    ::foundation::linear::drop(std::move(back));
}

// A generic body sees the view of the brand of the source, and an erased
// source, the singleton root of its tag, lends an erased view.  A branded
// source lends no erased view, which
// neg_brand_read_view_body_asks_the_erased_view shows.
void test_door_lends_the_view_of_the_source_brand(::fixy::TestRunnerCtx const& test) {
    auto perm = mint_permission_root<ConfigData>();
    using Brand = decltype(perm)::brand_type;
    auto [branded, back] = with_read_view(std::move(perm), [](auto const& view) noexcept {
        return std::is_same_v<std::remove_cvref_t<decltype(view)>, ReadView<ConfigData, Brand>>;
    });
    auto [erased, erased_back] =
        with_read_view(mint_singleton_root<test_read_view_singleton::ErasedData>(), [](auto const& view) noexcept {
            return ::foundation::brand::IsErased<std::remove_cvref_t<decltype(view)>>;
        });
    CRUCIBLE_TEST_ASSERT(test, branded);
    CRUCIBLE_TEST_ASSERT(test, erased);
    ::foundation::linear::drop(std::move(back));
    release_singleton_root(std::move(erased_back));
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

// The view that a fork lends a body: an empty proof that no code outside
// the write borrow door builds, that cannot leave the body, and that is no
// token.
struct WriteViewBrand {};
using ForkView = WriteView<fork_tags::Left, WriteViewBrand>;
static_assert(sizeof(ForkView) == 1, "a WriteView is an empty proof");
static_assert(!std::is_default_constructible_v<ForkView>, "only the write borrow door builds a WriteView");
static_assert(!std::is_copy_constructible_v<ForkView> && !std::is_move_constructible_v<ForkView>,
              "a WriteView cannot leave the body that the fork lends it to");
static_assert(!std::is_copy_assignable_v<ForkView> && !std::is_move_assignable_v<ForkView>);
static_assert(!std::is_trivially_copyable_v<ForkView> && !std::is_implicit_lifetime_v<ForkView>,
              "no byte route builds a WriteView");
static_assert(!std::is_constructible_v<Permission<fork_tags::Left, WriteViewBrand>, ForkView const&>
                  && !std::is_constructible_v<Permission<fork_tags::Left, WriteViewBrand>, ForkView&&>,
              "a WriteView does not become a token");

}  // namespace

namespace {

namespace eff = ::foundation::effects;

// The inline arm runs in any context, here the foreground one.  The arm
// that starts threads is tested in test/fixy/test_concurrent_permission_fork.cpp.
using HotFgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

// A fork on the inline arm inside the door: the bodies run in child order
// on this thread, and the foreground context needs no background effect.
void test_fork_inline_inside_the_door(::fixy::TestRunnerCtx const& test) {
    auto config_perm = mint_permission_root<ConfigData>();

    int order = 0;
    int left_seen_at = 0;
    int right_seen_at = 0;

    auto whole = mint_permission_root<fork_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;

    auto [rebuilt, config_back] = with_read_view(std::move(config_perm), [&](auto const& cv) {
        return mint_permission_fork_inline<fork_tags::Left, fork_tags::Right>(
            eff::testing::foreground(), std::move(whole),
            [&cv, &order, &left_seen_at](WriteView<fork_tags::Left, WholeBrand> const&, HotFgCtx const&) noexcept {
                (void)cv;
                left_seen_at = ++order;
            },
            [&cv, &order, &right_seen_at](WriteView<fork_tags::Right, WholeBrand> const&, HotFgCtx const&) noexcept {
                (void)cv;
                right_seen_at = ++order;
            });
    });

    CRUCIBLE_TEST_ASSERT(test, left_seen_at == 1);
    CRUCIBLE_TEST_ASSERT(test, right_seen_at == 2);

    ::foundation::linear::drop(std::move(rebuilt));
    ::foundation::linear::drop(std::move(config_back));
}

// A body of a borrow and of a fork gives its value back (the value of a
// body): the borrow gives the value beside the token, and the fork gives the
// value of each body in child order, with Unit for a body that gives
// nothing, and the parent last.
void test_fork_gives_the_value_of_each_body(::fixy::TestRunnerCtx const& test) {
    auto left = mint_permission_root<fork_tags::Left>();
    using LeftBrand = ::foundation::brand::brand_of_t<decltype(left)>;
    auto [lent_value, left_back] =
        with_write_view(eff::testing::foreground(), std::move(left),
                        [](WriteView<fork_tags::Left, LeftBrand> const&, HotFgCtx const&) noexcept { return 7; });
    CRUCIBLE_TEST_ASSERT(test, lent_value == 7);
    ::foundation::linear::drop(std::move(left_back));

    auto whole = mint_permission_root<fork_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    int order = 0;
    auto [left_order, right_unit, rebuilt] = mint_permission_fork_inline<fork_tags::Left, fork_tags::Right>(
        eff::testing::foreground(), std::move(whole),
        [&order](WriteView<fork_tags::Left, WholeBrand> const&, HotFgCtx const&) noexcept { return ++order; },
        [&order](WriteView<fork_tags::Right, WholeBrand> const&, HotFgCtx const&) noexcept { ++order; });
    static_assert(std::is_same_v<decltype(right_unit), ::foundation::core::Unit>);
    CRUCIBLE_TEST_ASSERT(test, left_order == 1 && order == 2);
    ::foundation::linear::drop(std::move(rebuilt));
}

// A view from a live share.  The door holds the guard while the body
// runs and hands it back, so the share stays counted throughout, and
// the pool cannot upgrade until the guard ends.
void test_door_over_a_share_guard(::fixy::TestRunnerCtx const& test) {
    SharedPermissionPool pool{mint_permission_root<ConfigData>()};
    {
        auto guard = pool.lend(test).expect("an idle pool lends a share");
        auto [counted, held] = with_read_view(std::move(guard), [&pool](auto const& view) noexcept {
            static_assert(std::is_same_v<typename std::remove_cvref_t<decltype(view)>::tag_type, ConfigData>);
            return pool.outstanding();
        });
        CRUCIBLE_TEST_ASSERT(test, counted == 1);
        CRUCIBLE_TEST_ASSERT(test, held.holds_share());
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);
        // The refused upgrade gives an empty Option, and expect_none ends
        // it.  A granted upgrade would end the process, so the test fails.
        pool.try_upgrade(test).expect_none("the live guard blocks the upgrade");
    }
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);
    auto exclusive = pool.try_upgrade(test);
    CRUCIBLE_TEST_ASSERT(test, exclusive.is_some());
    ::foundation::linear::drop(std::move(exclusive).expect("the test above found the exclusive"));
}

// A loan travels to a reader on another thread.  The lender holds a
// LentPermission, which gives no access, until the loan comes back.  The
// reader runs on the thread door, and the join of the door orders its
// reads before the check.
void test_loan_across_threads(::fixy::TestRunnerCtx const& test) {
    auto token = mint_permission_root<ConfigData>();
    auto [loan, lent] = mint_read_loan(std::move(token));

    ::fixy::Atomic<std::uint32_t, ::fixy::Counter> reads{0};
    ReadLoan<ConfigData, decltype(token)::brand_type> returned = [&] {
        auto reader_loan = std::move(loan);
        auto reader = [&reads, &reader_loan] noexcept {
            for (int round = 0; round < 3; ++round) {
                reader_loan = with_read_view(std::move(reader_loan), [&reads](auto const&) noexcept {
                    static_cast<void>(reads.fetch_add_relaxed(1));
                });
            }
        };
        ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
        ::fixy::thread::run_joined_raw_probe(ctx, test, "the reader takes the loan by reference on purpose",
                                             ::fixy::thread::ThreadOptions{}, std::move(reader))
            .expect("the thread door starts the reader");
        return reader_loan;
    }();

    CRUCIBLE_TEST_ASSERT(test, reads.load_acquire() == 3);
    auto back = mint_permission_after_loan(std::move(lent), std::move(returned));
    static_assert(std::is_same_v<decltype(back), decltype(token)>, "the token comes back with its own brand");
    ::foundation::linear::drop(std::move(back));
}

// A handle keeps a write token and a read loan, each with the brand of its
// region.  Both are empty and collapse, so the handle carries no payload.
template <typename SliceBrand, typename ConfigBrand>
struct WorkerHandle {
    [[no_unique_address]] Permission<WorkerSlice, SliceBrand> write_perm;
    [[no_unique_address]] ReadLoan<ConfigData, ConfigBrand> config_loan;

    constexpr WorkerHandle(Permission<WorkerSlice, SliceBrand>&& wp, ReadLoan<ConfigData, ConfigBrand>&& loan) noexcept
        : write_perm{std::move(wp)}, config_loan{std::move(loan)} {}
};

void test_handle_composition_zero_cost() {
    auto [loan, lent] = mint_read_loan(mint_permission_root<ConfigData>());
    WorkerHandle handle{mint_permission_root<WorkerSlice>(), std::move(loan)};
    static_assert(sizeof(handle) <= 2, "a handle of two empty proof tokens must be at most 2 bytes");
    auto config = mint_permission_after_loan(std::move(lent), std::move(handle.config_loan));
    ::foundation::linear::drop(std::move(config));
    ::foundation::linear::drop(std::move(handle.write_perm));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    pass(test, "test_read_view:\n");

    // Nothing to observe at runtime, so this one is called directly.
    test_compile_time_properties();

    test_door_void_body_hands_the_source_back(test);
    pass(test, "  test_door_void_body_hands_the_source_back: PASSED\n");
    test_door_value_body(test);
    pass(test, "  test_door_value_body: PASSED\n");
    test_door_struct_body(test);
    pass(test, "  test_door_struct_body: PASSED\n");
    test_door_lends_the_view_of_the_source_brand(test);
    pass(test, "  test_door_lends_the_view_of_the_source_brand: PASSED\n");
    test_fork_inline_inside_the_door(test);
    test_fork_gives_the_value_of_each_body(test);
    pass(test, "  test_fork_inline_inside_the_door: PASSED\n");
    test_door_over_a_share_guard(test);
    pass(test, "  test_door_over_a_share_guard: PASSED\n");
    test_loan_across_threads(test);
    pass(test, "  test_loan_across_threads: PASSED\n");
    test_handle_composition_zero_cost();
    pass(test, "  test_handle_composition_zero_cost: PASSED\n");
    pass(test, "test_read_view: ALL PASSED\n");
    return 0;
}
