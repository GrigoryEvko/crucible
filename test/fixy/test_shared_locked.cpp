// A shared lock that owns its data (SharedLocked of fixy/os/SharedLocked.h).  A
// read body gets the data as const under a share, a write body gets it under
// the lease of the exclusive, a write does not start while a read body runs,
// and a read does not start while a write body runs.

#include <fixy/os/SharedLocked.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

struct CountTag final {
    using permission_row = eff::Row<>;
};

// The value of an Option of a count, or zero.
[[nodiscard]] std::uint64_t count_or_zero(::fixy::Option<std::uint64_t> const& count) noexcept {
    return count.match([](std::uint64_t value) noexcept { return value; }, []() noexcept { return std::uint64_t{0}; });
}

// True when the Option holds true.
[[nodiscard]] bool holds_true(::fixy::Option<bool> const& answer) noexcept {
    return answer.match([](bool value) noexcept { return value; }, []() noexcept { return false; });
}

void check_shared_locked() {
    ::fixy::TestRunnerCtx const ctx{eff::testing::test()};
    auto root = perm::mint_permission_root<CountTag>();
    using Brand = typename decltype(root)::brand_type;
    ::fixy::SharedLocked<std::uint64_t, CountTag, Brand> shared{std::move(root), std::uint64_t{5}};

    // A read body gets the data as const, and a write body changes it.
    CRUCIBLE_TEST_ASSERT(
        kTest, count_or_zero(shared.with_read(ctx, [](std::uint64_t const& value) noexcept { return value; })) == 5);
    CRUCIBLE_TEST_ASSERT(kTest, shared.with_write(ctx, [](std::uint64_t& value) noexcept { value += 2; }).is_some());

    // The upgrade of a write fails while a read body runs, so the write
    // does not start and its body does not run.
    ::fixy::Option<bool> const is_write_refused =
        shared.with_read(ctx, [&shared, &ctx](std::uint64_t const& /*value*/) noexcept {
            return shared.with_write(ctx, [](std::uint64_t& value) noexcept { value = 0; }).is_none();
        });
    CRUCIBLE_TEST_ASSERT(kTest, holds_true(is_write_refused));

    // A share fails while a write body runs, so the read does not start.
    ::fixy::Option<bool> const is_read_refused =
        shared.with_write(ctx, [&shared, &ctx](std::uint64_t& /*value*/) noexcept {
            return shared.with_read(ctx, [](std::uint64_t const& value) noexcept { return value; }).is_none();
        });
    CRUCIBLE_TEST_ASSERT(kTest, holds_true(is_read_refused));

    // Two read bodies run one inside the other: the pool lends two shares.
    ::fixy::Option<bool> const is_second_read_given =
        shared.with_read(ctx, [&shared, &ctx](std::uint64_t const& /*value*/) noexcept {
            return shared.with_read(ctx, [](std::uint64_t const& value) noexcept { return value; }).is_some();
        });
    CRUCIBLE_TEST_ASSERT(kTest, holds_true(is_second_read_given));

    // No refused body changed the data, and each door gave its share or its
    // lease back.
    CRUCIBLE_TEST_ASSERT(
        kTest, count_or_zero(shared.with_read(ctx, [](std::uint64_t const& value) noexcept { return value; })) == 7);
}

}  // namespace

int main() {
    check_shared_locked();
    ::fixy::check::pass(kTest, "test_shared_locked: PASSED\n");
    return 0;
}
