// The spin budget of fixy/os/SpinBudget.h: the rate of a counter from fixed
// counts, the conversion of a budget to ticks, the rate of the process, the
// opaque deadline, the two spin forms, the gate of Nondet, and the rule of
// Determinism.
//
// The determinism test receives one stream of values with a budget of zero,
// which takes the slower wait after each empty poll, and with the largest
// budget, which spins.  The two runs give the same values, bit for bit.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/SpinBudget.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Time.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/SpinHint.h>
#include <foundation/permissions/Permission.h>

#include "../foundation/philox_stream.h"

#include <cstdint>

namespace {

namespace cc = ::fixy::concurrent;
namespace eff = ::foundation::effects;
namespace core = ::foundation::core;
namespace os = ::foundation::os;
namespace perm = ::foundation::permissions;
namespace spin = ::fixy::time;
using spin::CounterTicks;
using spin::SpanNanos;
using spin::SpinBudget;
using spin::SpinDeadline;
using spin::TickRate;

constexpr std::uint64_t kNanosecondsPerSecond = 1000000000U;
constexpr SpanNanos kOneSecond{kNanosecondsPerSecond};
constexpr SpinBudget kLargestBudget{~std::uint32_t{0}};

// The gate: a deadline takes the bare tag of Nondet.  A load context and the
// context of a test give the tag, and the foreground and a drain context do
// not.
static_assert(eff::CtxOwnsCapability<::fixy::BgLoadCtx, eff::Effect::Nondet>);
static_assert(eff::CtxOwnsCapability<::fixy::TestRunnerCtx, eff::Effect::Nondet>);
static_assert(!eff::CtxOwnsCapability<::fixy::HotFgCtx, eff::Effect::Nondet>);
static_assert(!eff::CtxOwnsCapability<::fixy::BgDrainCtx, eff::Effect::Nondet>);

// The rate of a counter of 1 GHz, 2.4 GHz and 25 MHz, with the largest shift
// that keeps the multiplier below 2^32, and the ticks of a budget at each
// rate.
void test_the_rate_of_fixed_counts() {
    constexpr TickRate one_ghz = spin::tick_rate_of(CounterTicks{kNanosecondsPerSecond}, kOneSecond);
    static_assert(one_ghz == TickRate{.multiplier = std::uint32_t{1} << 31, .shift = 31});
    static_assert(spin::ticks_of(one_ghz, SpinBudget{1000}) == CounterTicks{1000});

    constexpr TickRate tsc = spin::tick_rate_of(CounterTicks{2400000000U}, kOneSecond);
    static_assert(tsc == TickRate{.multiplier = 2576980378U, .shift = 30});
    static_assert(spin::ticks_of(tsc, SpinBudget{1000000}) == CounterTicks{2400000});

    constexpr TickRate generic_timer = spin::tick_rate_of(CounterTicks{25000000U}, kOneSecond);
    static_assert(generic_timer == TickRate{.multiplier = 3435973837U, .shift = 37});
    static_assert(spin::ticks_of(generic_timer, SpinBudget{1000000}) == CounterTicks{25000});

    // A counter of 5 GHz converts the largest budget with no overflow.
    constexpr TickRate fast = spin::tick_rate_of(CounterTicks{5000000000U}, kOneSecond);
    static_assert(fast.shift == 29);
    static_assert(spin::ticks_of(fast, kLargestBudget) == CounterTicks{21474836475U});

    static_assert(spin::tick_rate_of(CounterTicks{0}, kOneSecond) == TickRate{});
    static_assert(spin::tick_rate_of(CounterTicks{kNanosecondsPerSecond}, SpanNanos{0}) == TickRate{});
    static_assert(spin::tick_rate_of(CounterTicks{~std::uint64_t{0}}, SpanNanos{1}) == TickRate{});
    static_assert(spin::ticks_of(TickRate{}, kLargestBudget) == CounterTicks{0});
}

// The start-up hook gave the process a rate between 1 MHz and 10 GHz.
void test_the_process_has_a_rate(::fixy::BgLoadCtx const& ctx) {
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    std::uint64_t const ticks_of_a_second =
        spin::ticks_of(spin::tick_rate_of_process(nondet),
                       SpinBudget{static_cast<std::uint32_t>(kNanosecondsPerSecond)})
            .raw();
    CRUCIBLE_FATAL_INVARIANT(ticks_of_a_second >= 1000000U);
    CRUCIBLE_FATAL_INVARIANT(ticks_of_a_second <= 10000000000U);
}

// A budget of zero gives a deadline that passed, and the largest budget a
// deadline that did not.
void test_a_deadline_after_a_budget(::fixy::BgLoadCtx const& ctx) {
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    CRUCIBLE_FATAL_INVARIANT(SpinDeadline::passed().has_passed(nondet));
    CRUCIBLE_FATAL_INVARIANT(SpinDeadline::after(nondet, SpinBudget{0}).has_passed(nondet));
    CRUCIBLE_FATAL_INVARIANT(!SpinDeadline::after(nondet, kLargestBudget).has_passed(nondet));
}

// A spin that ends at a time budget carries Nondet and no Block, so a
// context that owns only its own atom and Nondet runs it.
void test_a_spin_under_a_nondet_row() {
    using NondetCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Nondet>>;
    NondetCtx const ctx{eff::testing::test()};
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    SpinDeadline const passed = SpinDeadline::after(nondet, SpinBudget{0});
    CRUCIBLE_FATAL_INVARIANT(spin::spin_until(nondet, passed, []() noexcept { return true; }));
    CRUCIBLE_FATAL_INVARIANT(!spin::spin_until(nondet, passed, []() noexcept { return false; }));
}

// Each form ends at the change or at the deadline.  A predicate that holds
// gives true also with a deadline that passed.
void test_the_two_forms(::fixy::BgLoadCtx const& ctx) {
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    SpinDeadline const passed = SpinDeadline::after(nondet, SpinBudget{0});
    CRUCIBLE_FATAL_INVARIANT(spin::spin_until(nondet, passed, []() noexcept { return true; }));
    CRUCIBLE_FATAL_INVARIANT(!spin::spin_until(nondet, passed, []() noexcept { return false; }));
    core::Atomic<std::uint32_t> const word{4};
    CRUCIBLE_FATAL_INVARIANT(spin::spin_while_equal(nondet, word, 9, passed) == os::SpinEnd::changed);
    CRUCIBLE_FATAL_INVARIANT(spin::spin_while_equal(nondet, word, 4, passed) == os::SpinEnd::deadline_passed);
    CRUCIBLE_FATAL_INVARIANT(spin::spin_while_equal(nondet, word, 9, SpinDeadline::after(nondet, kLargestBudget))
                             == os::SpinEnd::changed);
}

// The fold of one value into the hash of a stream.
[[nodiscard]] constexpr std::uint64_t fold(std::uint64_t hash, std::uint64_t value) noexcept {
    constexpr std::uint64_t kPrime = 0x100000001B3U;
    return (hash ^ value) * kPrime;
}

constexpr std::uint64_t kSeed = 0x5EED5EED5EEDU;
constexpr std::uint32_t kValues = 2000;
constexpr std::uint64_t kSleepNanos = 20000;

// The result of one receive of the stream: the hash of the values, and the
// count of the slower waits.
struct Received {
    std::uint64_t hash = 0;
    std::uint64_t sleeps = 0;
};

// The tag of the channel of the stream.
struct StreamTag {};

// The producer of the stream, on a thread of its own.  It pushes the values
// of a Philox stream, with a pause of some turns before each push.  The end
// of its handle ends the stream.  The thread calls it one time as an rvalue,
// so the call operator gives up its handle.
template <class Producer>
struct StreamFeed {
    Producer handle;

    void operator()() && noexcept {
        Producer local = std::move(handle);
        ::crucible::test::PhiloxStream stream{kSeed};
        ::crucible::test::PhiloxStream delays{kSeed + 1};
        for (std::uint32_t index = 0; index < kValues; ++index) {
            std::uint64_t const value = stream.next_wide();
            for (std::uint64_t turn = delays.below(2000); turn > 0; --turn) {
                CRUCIBLE_SPIN_PAUSE;
            }
            while (!local.try_push(value)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        ::foundation::linear::drop(std::move(local));
    }
};

// The consumer of the stream, on a thread of its own.  It polls the channel.
// After each empty poll it spins with its budget, and when the budget passes
// it sleeps for kSleepNanos and polls again.  It gives back its result, which
// the caller reads after the join.
template <class Consumer>
struct StreamDrain {
    Consumer handle;
    SpinBudget budget{};

    StreamDrain(Consumer&& consumer, SpinBudget spin_budget) noexcept
        : handle{std::move(consumer)}, budget{spin_budget} {}

    // The thread calls the drain one time as an rvalue, and the drain ends
    // its handle at the drop door.
    [[nodiscard]] Received operator()(::fixy::BgLoadCtx const& thread_ctx) && noexcept {
        Received received{};
        eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(thread_ctx);
        auto const sleeper = spin::mint_bounded_sleep<kSleepNanos>(thread_ctx);
        for (std::uint32_t index = 0; index < kValues; ++index) {
            core::Option<std::uint64_t> item = handle.try_pop();
            while (item.is_none()) {
                SpinDeadline const deadline = SpinDeadline::after(nondet, budget);
                if (!spin::spin_until(nondet, deadline,
                                      [this, nondet]() noexcept { return !handle.empty_approx(nondet); })) {
                    sleeper.sleep_for(kSleepNanos).expect("a sleep on the monotonic clock is valid");
                    ++received.sleeps;
                }
                item = handle.try_pop();
            }
            received.hash = fold(received.hash, std::move(item).expect("the loop ends with an item"));
        }
        ::foundation::linear::drop(std::move(handle));
        return received;
    }
};

// A producer and a consumer of the stream, each with one end of a wire
// channel, with the consumer budget.
[[nodiscard]] Received receive_the_stream(::fixy::BgLoadCtx const& ctx, SpinBudget budget) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, StreamTag>>();
    using Stream = cc::channel_t<std::uint64_t, 64, decltype(root)>;
    Stream channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Stream::producer_tag, Stream::consumer_tag>(std::move(root));
    return cc::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                         [&ctx, budget](auto producer, auto consumer) noexcept {
                             StreamFeed feed{std::move(producer)};
                             StreamDrain drain{std::move(consumer), budget};
                             // The door owns the two bodies, and it ends each linear body after the join.
                             auto [fed, received] = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{},
                                                                               std::move(feed), std::move(drain))
                                                        .expect("the test starts the producer thread and the "
                                                                "consumer thread");
                             static_cast<void>(fed);
                             return received;
                         });
}

// Determinism: the budget picks a spin or a sleep, and the values do not
// change.  The run with a budget of zero takes the sleep at least one time,
// so the test reads the two paths.
void test_the_budget_does_not_change_the_result(::fixy::BgLoadCtx const& ctx) {
    std::uint64_t expected = 0;
    ::crucible::test::PhiloxStream stream{kSeed};
    for (std::uint32_t index = 0; index < kValues; ++index) {
        expected = fold(expected, stream.next_wide());
    }
    Received const with_no_spin = receive_the_stream(ctx, SpinBudget{0});
    Received const with_a_long_spin = receive_the_stream(ctx, kLargestBudget);
    CRUCIBLE_FATAL_INVARIANT(with_no_spin.hash == expected);
    CRUCIBLE_FATAL_INVARIANT(with_a_long_spin.hash == expected);
    CRUCIBLE_FATAL_INVARIANT(with_no_spin.sleeps > 0);
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    test_the_rate_of_fixed_counts();
    test_the_process_has_a_rate(ctx);
    test_a_deadline_after_a_budget(ctx);
    test_a_spin_under_a_nondet_row();
    test_the_two_forms(ctx);
    test_the_budget_does_not_change_the_result(ctx);
    return 0;
}
