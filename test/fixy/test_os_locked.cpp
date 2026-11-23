// Locks that own their data (Locked of fixy/os/SpinLock.h).  A body gets the
// data only inside the lock, a guard holds the lock across some bodies, a try
// runs no body while another holder has the lock, and four threads that add
// to one count under the lock lose no step.
//
// The cells that prove a door is closed cannot live here: a private member
// access is a compile error.  They are the fixtures neg_locked_*.

#include <fixy/os/SpinLock.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace spin = ::fixy::spin;

constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

// A context that may spin: it owns Div, and neither IO nor Block.
using SpinnerCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Div>>;

struct CountTag final {};

// The data of a lock in the tests: a count and the count of the bodies.
struct Counts {
    std::uint64_t added = 0;
    std::uint64_t bodies = 0;
};

// The doors of one lock with the context that its strategy admits.
template <class Lock, class Ctx>
void check_doors(Ctx const& ctx) {
    Lock locked{};

    // with lends the data, and gives the value of the body.
    locked.with(ctx, [](Counts& counts) noexcept { counts.added += 2; });
    std::uint64_t const added = locked.with(ctx, [](Counts const& counts) noexcept { return counts.added; });
    CRUCIBLE_TEST_ASSERT(kTest, added == 2);

    // A guard holds the lock across two bodies.  A try runs no body while the
    // guard holds the lock.
    {
        auto guard = locked.lock(ctx);
        guard.with([](Counts& counts) noexcept { ++counts.bodies; });
        guard.with([](Counts& counts) noexcept { ++counts.bodies; });
        CRUCIBLE_TEST_ASSERT(kTest, locked.try_with(ctx, [](Counts& counts) noexcept { ++counts.bodies; }).is_none());
    }

    // The end of the guard gave the lock back, so the try takes it.
    ::fixy::Option<std::uint64_t> const bodies =
        locked.try_with(ctx, [](Counts const& counts) noexcept { return counts.bodies; });
    CRUCIBLE_TEST_ASSERT(kTest, bodies.is_some());
    CRUCIBLE_TEST_ASSERT(kTest, bodies.match([](std::uint64_t count) noexcept { return count; },
                                             []() noexcept { return std::uint64_t{0}; })
                                    == 2);

    // A void body of a try gives Unit.
    CRUCIBLE_TEST_ASSERT(kTest, locked.try_with(ctx, [](Counts& counts) noexcept { ++counts.bodies; }).is_some());

    // A const lock lends the data as const, through each door.
    Lock const& view = locked;
    CRUCIBLE_TEST_ASSERT(kTest, view.with(ctx, [](Counts const& counts) noexcept { return counts.bodies; }) == 3);
    {
        auto read_guard = view.lock(ctx);
        CRUCIBLE_TEST_ASSERT(kTest, read_guard.with([](Counts const& counts) noexcept { return counts.added; }) == 2);
    }
}

// Four threads add to one count under a blocking lock.  The count is plain,
// so a torn step would show as a short total.  Four threads against one
// holder put waiters to sleep, so the release that wakes a sleeper runs too.
void check_blocking_lock_excludes(::fixy::BgLoadCtx const& bg) {
    constexpr std::uint64_t per_thread = 20000;
    spin::BlockingLocked<Counts, CountTag> locked{};
    auto adder = [&locked](::fixy::BgLoadCtx const& ctx) noexcept {
        for (std::uint64_t step = 0; step < per_thread; ++step) {
            locked.with(ctx, [](Counts& counts) noexcept { ++counts.added; });
        }
    };
    ::fixy::thread::run_joined_raw_probe(bg, kTest, "the four threads add to one count of one lock on purpose",
                                         ::fixy::thread::ThreadOptions{}, auto{adder}, auto{adder}, auto{adder},
                                         std::move(adder))
        .expect("the thread door starts each adder");
    CRUCIBLE_TEST_ASSERT(kTest,
                         locked.with(bg, [](Counts const& counts) noexcept { return counts.added; }) == 4 * per_thread);
}

// The same for a spin lock.  Each thread makes a context that may spin, and
// a body of the thread door holds no context.
void check_spin_lock_excludes(::fixy::BgLoadCtx const& bg) {
    constexpr std::uint64_t per_thread = 20000;
    spin::SpinLocked<Counts, CountTag> locked{};
    auto adder = [&locked]() noexcept {
        SpinnerCtx const spinner{eff::testing::test()};
        for (std::uint64_t step = 0; step < per_thread; ++step) {
            locked.with(spinner, [](Counts& counts) noexcept { ++counts.added; });
        }
    };
    ::fixy::thread::run_joined_raw_probe(bg, kTest, "the four threads add to one count of one lock on purpose",
                                         ::fixy::thread::ThreadOptions{}, auto{adder}, auto{adder}, auto{adder},
                                         std::move(adder))
        .expect("the thread door starts each adder");
    SpinnerCtx const spinner{eff::testing::test()};
    CRUCIBLE_TEST_ASSERT(kTest, locked.with(spinner, [](Counts const& counts) noexcept { return counts.added; })
                                    == 4 * per_thread);
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const bg{eff::testing::bg()};
    check_doors<spin::SpinLocked<Counts, CountTag>>(SpinnerCtx{eff::testing::test()});
    check_doors<spin::BlockingLocked<Counts, CountTag>>(bg);
    check_blocking_lock_excludes(bg);
    check_spin_lock_excludes(bg);
    ::fixy::check::pass(kTest, "test_os_locked: PASSED\n");
    return 0;
}
