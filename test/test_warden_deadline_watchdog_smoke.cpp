// Every watchdog built below is given no observation source, which is
// how it is constructed in a unit test and on a machine without the
// kernel support.  In that configuration it must report that it has
// no signal, rather than reporting health it cannot have measured.

#include <crucible/perf/Senses.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

// The two constants below are asserted here because this file is
// their only consumer, and an unused constant is an error under the
// project's warning flags.
static_assert(crucible::perf::TIMELINE_MASK == crucible::perf::TIMELINE_CAPACITY - 1,
              "TIMELINE_MASK is the cyclic-mask companion to TIMELINE_CAPACITY");
static_assert(crucible::perf::PMU_SAMPLE_MASK == crucible::perf::PMU_SAMPLE_CAPACITY - 1,
              "PMU_SAMPLE_MASK is the cyclic-mask companion to PMU_SAMPLE_CAPACITY");

namespace {

using crucible::warden::DeadlineWatchdog;
using crucible::warden::Policy;
using crucible::warden::SchedClass;
using crucible::warden::WatchdogVerdict;
using crucible::warden::demote_one_step;
using crucible::warden::watchdog_verdict_name;

static_assert(static_cast<int>(WatchdogVerdict::InsufficientData) == 0,
              "InsufficientData must be the zero sentinel — default-construction "
              "of a verdict-typed field gives 'no signal' rather than a false "
              "Healthy");
static_assert(static_cast<int>(WatchdogVerdict::Healthy) == 1, "Healthy ordinal pinned for stable serialization");
static_assert(static_cast<int>(WatchdogVerdict::Downgrade) == 2, "Downgrade ordinal pinned for stable serialization");
static_assert(sizeof(WatchdogVerdict) == 1, "WatchdogVerdict packs into one byte (uint8_t underlying)");

static_assert(!std::is_copy_constructible_v<DeadlineWatchdog>,
              "DeadlineWatchdog rolling-window state must not be copied; "
              "copying would shadow a stale window over a new one");
static_assert(!std::is_copy_assignable_v<DeadlineWatchdog>, "see copy-ctor rationale");
static_assert(std::is_move_constructible_v<DeadlineWatchdog>,
              "A watchdog must be movable into the state struct that owns it.");
static_assert(std::is_move_assignable_v<DeadlineWatchdog>, "see move-ctor rationale");
static_assert(sizeof(DeadlineWatchdog) <= 64, "A watchdog must fit in one cache line, because callers place it "
                                              "on the stack beside other hot state.");

}  // namespace

int main() {
    int failures = 0;

    if (std::strcmp(watchdog_verdict_name(WatchdogVerdict::InsufficientData), "InsufficientData") != 0) {
        std::fprintf(stderr, "watchdog_verdict_name(InsufficientData) wrong: %s\n",
                     watchdog_verdict_name(WatchdogVerdict::InsufficientData));
        ++failures;
    }
    if (std::strcmp(watchdog_verdict_name(WatchdogVerdict::Healthy), "Healthy") != 0) {
        std::fprintf(stderr, "watchdog_verdict_name(Healthy) wrong: %s\n",
                     watchdog_verdict_name(WatchdogVerdict::Healthy));
        ++failures;
    }
    if (std::strcmp(watchdog_verdict_name(WatchdogVerdict::Downgrade), "Downgrade") != 0) {
        std::fprintf(stderr, "watchdog_verdict_name(Downgrade) wrong: %s\n",
                     watchdog_verdict_name(WatchdogVerdict::Downgrade));
        ++failures;
    }

    // With no observation source the verdict never changes, however
    // many times it is asked for.
    {
        Policy policy = Policy::production();  // a nonzero budget
        DeadlineWatchdog watchdog{/*senses=*/nullptr, policy, ::crucible::effects::testing::init()};
        for (int i = 0; i < 5; ++i) {
            const auto v = watchdog.observe(::crucible::effects::TestRunnerCtx{});
            if (v != WatchdogVerdict::InsufficientData) {
                std::fprintf(stderr,
                             "nullptr-Senses Watchdog should return "
                             "InsufficientData; got %s on iteration %d\n",
                             watchdog_verdict_name(v), i);
                ++failures;
            }
        }
        // No counter was ever read, so no baseline was ever taken and
        // every diagnostic is still at its initial value.
        if (watchdog.baseline_count() != 0u || watchdog.latest_count() != 0u || watchdog.window_started_ns() != 0u
            || watchdog.misses_in_window() != 0u) {
            std::fprintf(stderr,
                         "nullptr-Senses Watchdog diagnostics should be all "
                         "zero; got baseline=%llu latest=%llu window=%llu\n",
                         static_cast<unsigned long long>(watchdog.baseline_count()),
                         static_cast<unsigned long long>(watchdog.latest_count()),
                         static_cast<unsigned long long>(watchdog.window_started_ns()));
            ++failures;
        }
    }

    // A budget of zero turns the watchdog off outright.
    {
        Policy policy = Policy::none();
        policy.deadline_miss_budget = 0;
        DeadlineWatchdog watchdog{/*senses=*/nullptr, policy, ::crucible::effects::testing::init()};
        const auto v = watchdog.observe(::crucible::effects::TestRunnerCtx{});
        if (v != WatchdogVerdict::InsufficientData) {
            std::fprintf(stderr, "budget=0 should disable watchdog; got %s\n", watchdog_verdict_name(v));
            ++failures;
        }
        if (watchdog.miss_budget() != 0u) {
            std::fprintf(stderr, "budget getter returns %u; expected 0\n", watchdog.miss_budget());
            ++failures;
        }
    }

    // A window of zero turns it off too, independently of the budget.
    // Were it not disabled, every observation would see the window as
    // already elapsed and rebase itself, and the verdict would depend
    // on nothing but call timing.
    {
        Policy policy = Policy::production();
        policy.watchdog_window_sec = 0;
        DeadlineWatchdog watchdog{/*senses=*/nullptr, policy, ::crucible::effects::testing::init()};
        for (int i = 0; i < 5; ++i) {
            const auto v = watchdog.observe(::crucible::effects::TestRunnerCtx{});
            if (v != WatchdogVerdict::InsufficientData) {
                std::fprintf(stderr,
                             "a zero window should disable the watchdog; "
                             "got %s on iteration %d\n",
                             watchdog_verdict_name(v), i);
                ++failures;
            }
        }
        if (watchdog.window_ns() != 0u) {
            std::fprintf(stderr, "window_ns() = %llu; expected 0 for window_sec=0\n",
                         static_cast<unsigned long long>(watchdog.window_ns()));
            ++failures;
        }
    }

    // Demotion walks down the real-time classes one step at a time
    // and stops at the floor.  The idle class is not a weaker
    // real-time class but a separate thing, so it stays put.
    if (demote_one_step(SchedClass::Deadline) != SchedClass::Fifo) {
        std::fprintf(stderr, "demote(Deadline) wrong: expected Fifo\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Fifo) != SchedClass::Other) {
        std::fprintf(stderr, "demote(Fifo) wrong: expected Other\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::RoundRobin) != SchedClass::Other) {
        std::fprintf(stderr, "demote(RoundRobin) wrong: expected Other\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Other) != SchedClass::Other) {
        std::fprintf(stderr, "demote(Other) wrong: expected Other (already at floor)\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Batch) != SchedClass::Other) {
        std::fprintf(stderr, "demote(Batch) wrong: expected Other (same-tier downshift)\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Idle) != SchedClass::Idle) {
        std::fprintf(stderr, "demote(Idle) wrong: expected Idle (not an RT class)\n");
        ++failures;
    }

    {
        Policy policy = Policy::production();
        DeadlineWatchdog watchdog{/*senses=*/nullptr, policy, ::crucible::effects::testing::init()};
        if (watchdog.miss_budget() != policy.deadline_miss_budget) {
            std::fprintf(stderr, "miss_budget() = %u; expected %u\n", watchdog.miss_budget(),
                         policy.deadline_miss_budget);
            ++failures;
        }
        const uint64_t expected_window_ns = static_cast<uint64_t>(policy.watchdog_window_sec) * 1000000000ull;
        if (watchdog.window_ns() != expected_window_ns) {
            std::fprintf(stderr, "window_ns() = %llu; expected %llu\n",
                         static_cast<unsigned long long>(watchdog.window_ns()),
                         static_cast<unsigned long long>(expected_window_ns));
            ++failures;
        }
        if (watchdog.baseline_count() != 0u || watchdog.latest_count() != 0u || watchdog.window_started_ns() != 0u
            || watchdog.misses_in_window() != 0u) {
            std::fprintf(stderr, "fresh watchdog diagnostics not at zero\n");
            ++failures;
        }
    }

    // A reset returns the watchdog to the state it had before any
    // observation.
    {
        Policy policy = Policy::production();
        DeadlineWatchdog watchdog{/*senses=*/nullptr, policy, ::crucible::effects::testing::init()};
        (void)watchdog.observe(::crucible::effects::TestRunnerCtx{});  // no source, so no state changes
        watchdog.reset();
        if (watchdog.baseline_count() != 0u || watchdog.latest_count() != 0u || watchdog.window_started_ns() != 0u) {
            std::fprintf(stderr, "reset() should zero diagnostics\n");
            ++failures;
        }
    }

    // A watchdog moved into a new home keeps working.
    {
        Policy policy = Policy::production();
        DeadlineWatchdog watchdog_src{/*senses=*/nullptr, policy, ::crucible::effects::testing::init()};
        DeadlineWatchdog watchdog_sink = std::move(watchdog_src);
        const auto v = watchdog_sink.observe(::crucible::effects::TestRunnerCtx{});
        if (v != WatchdogVerdict::InsufficientData) {
            std::fprintf(stderr, "moved-into watchdog should still observe; got %s\n", watchdog_verdict_name(v));
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("warden::DeadlineWatchdog smoke OK\n");
    }
    return failures == 0 ? 0 : 1;
}
