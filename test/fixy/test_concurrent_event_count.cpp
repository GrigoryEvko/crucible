// The event count of fixy/concurrent/EventCount.h: the entry in the count,
// the wait that a notify ends, the deadline, the gate of the wait policy, and
// no lost wake under random delays.
//
// The stress test gives each wait a deadline of 10 s.  A lost wake ends that
// wait at its deadline, and the test then fails before the timeout of the
// test preset.

#include <fixy/Ctx.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/os/Futex.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Time.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>

#include "../foundation/philox_stream.h"

#include <cstdint>

namespace {

namespace eff = ::foundation::effects;
namespace core = ::foundation::core;
namespace cc = ::fixy::concurrent;
using cc::EventCount;
using cc::EventWait;
using cc::EventWaitEnd;
using ::fixy::futex::Deadline;

// A background context that owns Block and no Nondet.
using BlockOnlyCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

constexpr std::uint64_t kMillisecond = 1000000U;
constexpr std::uint64_t kLongDeadlineNanos = 10000U * kMillisecond;

// The policy of the stress test that parks at once, and the policy that
// spins for 2 microseconds first.
using ParkAtOnce = cc::SpinThenPark<0>;
using SpinFirst = cc::SpinThenPark<2000>;

[[nodiscard]] Deadline deadline_after(::fixy::BgLoadCtx const& ctx, std::uint64_t bound_nanos) noexcept {
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
    return Deadline::after(clock, eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx), bound_nanos)
        .expect("the monotonic clock gives a reading");
}

[[nodiscard]] bool has_waiters(::fixy::BgLoadCtx const& ctx, EventCount const& events) noexcept {
    return events.has_waiters(eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx));
}

// THE GATE: each context takes Spin, and only a context that owns Block and
// Nondet takes a policy that parks.
struct AlwaysHolds {
    [[nodiscard]] bool operator()() const noexcept { return true; }
};
static_assert(cc::CtxFitsWaitPolicy<::fixy::HotFgCtx, cc::Spin>);
static_assert(!cc::CtxFitsWaitPolicy<::fixy::HotFgCtx, ParkAtOnce>);
static_assert(!cc::CtxFitsWaitPolicy<BlockOnlyCtx, ParkAtOnce>);
static_assert(!cc::CtxFitsWaitPolicy<::fixy::BgObserveCtx, ParkAtOnce>);
static_assert(cc::CtxFitsWaitPolicy<::fixy::BgLoadCtx, ParkAtOnce>);
static_assert(cc::CtxFitsWaitPolicy<::fixy::BgLoadCtx, cc::Spin>);
static_assert(!cc::WaitPolicy<int>);

// The entry is in the count for its scope, and it leaves at the end.
void test_the_entry_leaves_at_its_end(::fixy::BgLoadCtx const& ctx) {
    EventCount events{};
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
    {
        EventWait const entry = events.prepare_wait();
        CRUCIBLE_FATAL_INVARIANT(has_waiters(ctx, events));
    }
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
}

// A notify after the entry changes the epoch, so the wait ends at once.  A
// wait with no notify ends at its deadline.
void test_a_notify_and_a_deadline_end_the_wait(::fixy::BgLoadCtx const& ctx) {
    EventCount events{};
    {
        EventWait const entry = events.prepare_wait();
        events.notify_one(ctx);
        CRUCIBLE_FATAL_INVARIANT(events.commit_wait(ctx, entry, core::none) == EventWaitEnd::notified);
    }
    {
        EventWait const entry = events.prepare_wait();
        events.notify_all(ctx);
        CRUCIBLE_FATAL_INVARIANT(events.commit_wait(ctx, entry, core::none) == EventWaitEnd::notified);
    }
    {
        EventWait const entry = events.prepare_wait();
        auto const deadline = core::Option<Deadline>::some(deadline_after(ctx, kMillisecond));
        CRUCIBLE_FATAL_INVARIANT(events.commit_wait(ctx, entry, deadline) == EventWaitEnd::deadline_passed);
    }
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
    // A notify with no waiter makes no call of the kernel and changes nothing.
    events.notify_all(ctx);
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
}

// Spin waits on the foreground, and a condition that holds ends each wait
// at once.
void test_a_condition_that_holds_ends_each_wait(::fixy::BgLoadCtx const& ctx) {
    EventCount events{};
    cc::wait_for<cc::Spin>(eff::testing::foreground(), events, AlwaysHolds{});
    cc::wait_for<ParkAtOnce>(ctx, events, AlwaysHolds{});
    CRUCIBLE_FATAL_INVARIANT(cc::wait_for<ParkAtOnce>(ctx, events, AlwaysHolds{}, deadline_after(ctx, 0))
                             == EventWaitEnd::notified);
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
}

// A condition that never holds ends a wait with a deadline at the deadline.
void test_a_wait_ends_at_its_deadline(::fixy::BgLoadCtx const& ctx) {
    EventCount events{};
    auto const never = []() noexcept { return false; };
    CRUCIBLE_FATAL_INVARIANT(cc::wait_for<SpinFirst>(ctx, events, never, deadline_after(ctx, kMillisecond))
                             == EventWaitEnd::deadline_passed);
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
}

// No wake is lost.  A producer publishes the rounds 1 to kRounds, each after
// a random delay of the Philox stream of SEED, and it notifies after each
// store.  Some delays are long, so the consumer parks after its budget.  The
// consumer waits for each round with a deadline of 10 s.
template <class Policy>
void expect_no_lost_wake(::fixy::BgLoadCtx const& ctx, std::uint64_t seed) {
    constexpr std::uint32_t kRounds = 3000;
    EventCount events{};
    core::Atomic<std::uint32_t> published{0};
    core::Atomic<std::uint32_t> late_rounds{0};
    auto producer = [&]() noexcept {
        ::crucible::test::PhiloxStream delays{seed};
        for (std::uint32_t round = 1; round <= kRounds; ++round) {
            std::uint64_t const draw = delays.below(100);
            std::uint64_t const turns = draw < 90 ? delays.below(500) : 2000 + delays.below(20000);
            for (std::uint64_t turn = 0; turn < turns; ++turn) {
                CRUCIBLE_SPIN_PAUSE;
            }
            published.store_release(round);
            events.notify_one(ctx);
        }
    };
    auto consumer = [&]() noexcept {
        for (std::uint32_t round = 1; round <= kRounds; ++round) {
            auto const is_published = [&published, round]() noexcept { return published.load_acquire() >= round; };
            if (cc::wait_for<Policy>(ctx, events, is_published, deadline_after(ctx, kLongDeadlineNanos))
                != EventWaitEnd::notified) {
                static_cast<void>(late_rounds.fetch_add_acq_rel(1));
                return;
            }
        }
    };
    CRUCIBLE_FATAL_INVARIANT(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, producer, consumer).is_ok());
    CRUCIBLE_FATAL_INVARIANT(late_rounds.load_acquire() == 0);
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
}

// No wake is lost with two consumers that wait on one count for the same
// rounds, and a producer that wakes each waiter.
void expect_no_lost_wake_of_two_waiters(::fixy::BgLoadCtx const& ctx, std::uint64_t seed) {
    constexpr std::uint32_t kRounds = 2000;
    EventCount events{};
    core::Atomic<std::uint32_t> published{0};
    core::Atomic<std::uint32_t> late_rounds{0};
    auto producer = [&]() noexcept {
        ::crucible::test::PhiloxStream delays{seed};
        for (std::uint32_t round = 1; round <= kRounds; ++round) {
            for (std::uint64_t turn = delays.below(3000); turn > 0; --turn) {
                CRUCIBLE_SPIN_PAUSE;
            }
            published.store_release(round);
            events.notify_all(ctx);
        }
    };
    auto consumer = [&]() noexcept {
        for (std::uint32_t round = 1; round <= kRounds; ++round) {
            auto const is_published = [&published, round]() noexcept { return published.load_acquire() >= round; };
            if (cc::wait_for<ParkAtOnce>(ctx, events, is_published, deadline_after(ctx, kLongDeadlineNanos))
                != EventWaitEnd::notified) {
                static_cast<void>(late_rounds.fetch_add_acq_rel(1));
                return;
            }
        }
    };
    auto second_consumer = consumer;
    CRUCIBLE_FATAL_INVARIANT(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, producer, consumer, second_consumer).is_ok());
    CRUCIBLE_FATAL_INVARIANT(late_rounds.load_acquire() == 0);
    CRUCIBLE_FATAL_INVARIANT(!has_waiters(ctx, events));
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    test_the_entry_leaves_at_its_end(ctx);
    test_a_notify_and_a_deadline_end_the_wait(ctx);
    test_a_condition_that_holds_ends_each_wait(ctx);
    test_a_wait_ends_at_its_deadline(ctx);
    expect_no_lost_wake<ParkAtOnce>(ctx, 0xE7E47C0U);
    expect_no_lost_wake<SpinFirst>(ctx, 0xE7E47C1U);
    expect_no_lost_wake_of_two_waiters(ctx, 0xE7E47C2U);
    return 0;
}
