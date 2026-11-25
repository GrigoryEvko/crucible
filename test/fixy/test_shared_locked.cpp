// A shared lock that owns its data (SharedLocked of fixy/os/SharedLocked.h).  A
// read body gets the data as const under a share, a write body gets it under
// the lease of the exclusive, a write does not start while a read body runs,
// and a read does not start while a write body runs.  Data that gives up what
// it holds has an end of its lock, and a lock that ended runs no body.

#include <fixy/os/SharedLocked.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Linear.h>
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

// Data that gives its count up at the end of its lock.
class LeavingCount final {
    std::uint64_t count_ = 0;

public:
    explicit constexpr LeavingCount(std::uint64_t count) noexcept : count_{count} {}

    void add(std::uint64_t amount) noexcept { count_ += amount; }

    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

    [[nodiscard]] std::uint64_t leave_lock(::fixy::SharedLockEndKey<LeavingCount> const& /*key*/) noexcept {
        return count_;
    }
};

// Asks for the end of the lock.  True when a share or a lease refuses it, or
// when the lock ended before.  An end that succeeds ends the exclusive, and
// gives false.
template <class Locked>
[[nodiscard]] bool is_end_refused(Locked& locked) {
    return locked.end_lock(kTest).match(
        [](auto ended) noexcept {
            auto [count, exclusive] = std::move(ended);
            static_cast<void>(count);
            ::foundation::linear::drop(std::move(exclusive));
            return false;
        },
        []() noexcept { return true; });
}

void check_end_of_lock() {
    auto root = perm::mint_permission_root<CountTag>();
    using Brand = typename decltype(root)::brand_type;
    ::fixy::SharedLocked<LeavingCount, CountTag, Brand> shared{std::move(root), std::uint64_t{5}};

    // A read body and a write body refuse the end, and the lock goes on.
    ::fixy::Option<bool> const is_refused_by_a_reader =
        shared.with_read(kTest, [&shared](LeavingCount const& /*count*/) noexcept { return is_end_refused(shared); });
    ::fixy::Option<bool> const is_refused_by_a_writer =
        shared.with_write(kTest, [&shared](LeavingCount& /*count*/) noexcept { return is_end_refused(shared); });
    CRUCIBLE_TEST_ASSERT(kTest, holds_true(is_refused_by_a_reader));
    CRUCIBLE_TEST_ASSERT(kTest, holds_true(is_refused_by_a_writer));
    CRUCIBLE_TEST_ASSERT(kTest, shared.with_write(kTest, [](LeavingCount& count) noexcept { count.add(2); }).is_some());

    auto [count, exclusive] = shared.end_lock(kTest).expect("no reader and no writer is in the lock");
    CRUCIBLE_TEST_ASSERT(kTest, count == 7);

    // Each door of a lock that ended gives no value.
    CRUCIBLE_TEST_ASSERT(
        kTest, shared.with_read(kTest, [](LeavingCount const& held) noexcept { return held.count(); }).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, shared.with_write(kTest, [](LeavingCount& held) noexcept { held.add(1); }).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, is_end_refused(shared));

    // The exclusive that came out opens a new lock.
    ::fixy::SharedLocked<LeavingCount, CountTag, Brand> next{std::move(exclusive), count};
    ::fixy::Option<std::uint64_t> const next_count =
        next.with_read(kTest, [](LeavingCount const& held) noexcept { return held.count(); });
    CRUCIBLE_TEST_ASSERT(kTest, next_count == ::fixy::some(std::uint64_t{7}));
}

}  // namespace

int main() {
    check_shared_locked();
    check_end_of_lock();
    ::fixy::check::pass(kTest, "test_shared_locked: PASSED\n");
    return 0;
}
