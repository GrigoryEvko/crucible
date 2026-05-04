// Sentinel TU for crucible::rt::DeadlineWatchdog — closes the
// SchedSwitch → Policy feedback loop opened by GAPS-004g (#1283).
//
// What this test asserts:
//   1. <crucible/rt/DeadlineWatchdog.h> reachable through the public
//      crucible include path.
//   2. WatchdogVerdict has the expected three-state shape
//      (InsufficientData / Healthy / Downgrade) and the diagnostic
//      string accessor returns stable names.
//   3. DeadlineWatchdog is move-only (deleted copy) and cache-line-
//      sized (≤64 B for stack-emplace anywhere).
//   4. Construction with `senses=nullptr` produces a permanently-
//      InsufficientData watchdog — used in unit tests and on systems
//      without libbpf.
//   5. Construction with `deadline_miss_budget=0` (Policy::none() /
//      operator opt-out) returns InsufficientData regardless of
//      Senses state — disabled is disabled.
//   6. demote_one_step() walks Deadline → Fifo → Other → Other and
//      preserves Idle (which is NOT a "weaker RT class" but a
//      separate semantic).
//   7. Diagnostics getters (baseline_count / latest_count /
//      window_started_ns / miss_budget / window_ns / misses_in_window)
//      all return their initial sentinel values on a fresh watchdog.
//   8. reset() returns the watchdog to the initial-baseline-pending
//      state.

#include <crucible/perf/Senses.h>
#include <crucible/rt/DeadlineWatchdog.h>
#include <crucible/rt/Policy.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

// ── Pull in the inline-constexpr constants from the included perf
// headers so -Werror=unused-const-variable doesn't fire when this TU
// is the only consumer.  Same workaround LockContention's smoke uses.
static_assert(crucible::perf::TIMELINE_MASK == crucible::perf::TIMELINE_CAPACITY - 1,
    "TIMELINE_MASK is the cyclic-mask companion to TIMELINE_CAPACITY");
static_assert(crucible::perf::PMU_SAMPLE_MASK == crucible::perf::PMU_SAMPLE_CAPACITY - 1,
    "PMU_SAMPLE_MASK is the cyclic-mask companion to PMU_SAMPLE_CAPACITY");

namespace {

using crucible::rt::DeadlineWatchdog;
using crucible::rt::Policy;
using crucible::rt::SchedClass;
using crucible::rt::WatchdogVerdict;
using crucible::rt::demote_one_step;
using crucible::rt::watchdog_verdict_name;

// ── (2) Verdict shape ──────────────────────────────────────────────

static_assert(static_cast<int>(WatchdogVerdict::InsufficientData) == 0,
    "InsufficientData must be the zero sentinel — default-construction "
    "of a verdict-typed field gives 'no signal' rather than a false "
    "Healthy");
static_assert(static_cast<int>(WatchdogVerdict::Healthy) == 1,
    "Healthy ordinal pinned for stable serialization");
static_assert(static_cast<int>(WatchdogVerdict::Downgrade) == 2,
    "Downgrade ordinal pinned for stable serialization");
static_assert(sizeof(WatchdogVerdict) == 1,
    "WatchdogVerdict packs into one byte (uint8_t underlying)");

// ── (3) Type-shape sanity ──────────────────────────────────────────

static_assert(!std::is_copy_constructible_v<DeadlineWatchdog>,
    "DeadlineWatchdog rolling-window state must not be copied; "
    "copying would shadow a stale window over a new one");
static_assert(!std::is_copy_assignable_v<DeadlineWatchdog>,
    "see copy-ctor rationale");
static_assert(std::is_move_constructible_v<DeadlineWatchdog>,
    "moving a DeadlineWatchdog into a Keeper state struct must work");
static_assert(std::is_move_assignable_v<DeadlineWatchdog>,
    "see move-ctor rationale");
static_assert(sizeof(DeadlineWatchdog) <= 64,
    "DeadlineWatchdog must fit in one cache line — its callers "
    "stack-allocate it next to other hot Keeper state");

}  // namespace

int main() {
    int failures = 0;

    // ── (2) Diagnostic name accessor ───────────────────────────────
    if (std::strcmp(watchdog_verdict_name(WatchdogVerdict::InsufficientData),
                    "InsufficientData") != 0) {
        std::fprintf(stderr,
            "watchdog_verdict_name(InsufficientData) wrong: %s\n",
            watchdog_verdict_name(WatchdogVerdict::InsufficientData));
        ++failures;
    }
    if (std::strcmp(watchdog_verdict_name(WatchdogVerdict::Healthy),
                    "Healthy") != 0) {
        std::fprintf(stderr, "watchdog_verdict_name(Healthy) wrong: %s\n",
            watchdog_verdict_name(WatchdogVerdict::Healthy));
        ++failures;
    }
    if (std::strcmp(watchdog_verdict_name(WatchdogVerdict::Downgrade),
                    "Downgrade") != 0) {
        std::fprintf(stderr, "watchdog_verdict_name(Downgrade) wrong: %s\n",
            watchdog_verdict_name(WatchdogVerdict::Downgrade));
        ++failures;
    }

    // ── (4) nullptr Senses → permanently InsufficientData ──────────
    {
        Policy policy = Policy::production();  // nonzero budget
        DeadlineWatchdog watchdog{
            /*senses=*/nullptr, policy, ::crucible::effects::Init{}};
        for (int i = 0; i < 5; ++i) {
            const auto v = watchdog.observe();
            if (v != WatchdogVerdict::InsufficientData) {
                std::fprintf(stderr,
                    "nullptr-Senses Watchdog should return "
                    "InsufficientData; got %s on iteration %d\n",
                    watchdog_verdict_name(v), i);
                ++failures;
            }
        }
        // Diagnostics should remain at sentinel zero — we never
        // captured a baseline because we never read a counter.
        if (watchdog.baseline_count() != 0u || watchdog.latest_count() != 0u
            || watchdog.window_started_ns() != 0u
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

    // ── (5) miss_budget=0 (disabled) → InsufficientData ────────────
    {
        Policy policy = Policy::none();  // budget unset by default
        policy.deadline_miss_budget = 0;
        DeadlineWatchdog watchdog{
            /*senses=*/nullptr, policy, ::crucible::effects::Init{}};
        const auto v = watchdog.observe();
        if (v != WatchdogVerdict::InsufficientData) {
            std::fprintf(stderr,
                "budget=0 should disable watchdog; got %s\n",
                watchdog_verdict_name(v));
            ++failures;
        }
        if (watchdog.miss_budget() != 0u) {
            std::fprintf(stderr,
                "budget getter returns %u; expected 0\n",
                watchdog.miss_budget());
            ++failures;
        }
    }

    // ── (6) demote_one_step walks Deadline → Fifo → Other ──────────
    if (demote_one_step(SchedClass::Deadline) != SchedClass::Fifo) {
        std::fprintf(stderr,
            "demote(Deadline) wrong: expected Fifo\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Fifo) != SchedClass::Other) {
        std::fprintf(stderr,
            "demote(Fifo) wrong: expected Other\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::RoundRobin) != SchedClass::Other) {
        std::fprintf(stderr,
            "demote(RoundRobin) wrong: expected Other\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Other) != SchedClass::Other) {
        std::fprintf(stderr,
            "demote(Other) wrong: expected Other (already at floor)\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Batch) != SchedClass::Other) {
        std::fprintf(stderr,
            "demote(Batch) wrong: expected Other (same-tier downshift)\n");
        ++failures;
    }
    if (demote_one_step(SchedClass::Idle) != SchedClass::Idle) {
        std::fprintf(stderr,
            "demote(Idle) wrong: expected Idle (not an RT class)\n");
        ++failures;
    }

    // ── (7) Diagnostics on fresh watchdog ──────────────────────────
    {
        Policy policy = Policy::production();
        DeadlineWatchdog watchdog{
            /*senses=*/nullptr, policy, ::crucible::effects::Init{}};
        if (watchdog.miss_budget() != policy.deadline_miss_budget) {
            std::fprintf(stderr,
                "miss_budget() = %u; expected %u\n",
                watchdog.miss_budget(), policy.deadline_miss_budget);
            ++failures;
        }
        const uint64_t expected_window_ns =
            static_cast<uint64_t>(policy.watchdog_window_sec)
            * 1'000'000'000ull;
        if (watchdog.window_ns() != expected_window_ns) {
            std::fprintf(stderr,
                "window_ns() = %llu; expected %llu\n",
                static_cast<unsigned long long>(watchdog.window_ns()),
                static_cast<unsigned long long>(expected_window_ns));
            ++failures;
        }
        // Pre-observe state
        if (watchdog.baseline_count() != 0u
            || watchdog.latest_count() != 0u
            || watchdog.window_started_ns() != 0u
            || watchdog.misses_in_window() != 0u) {
            std::fprintf(stderr,
                "fresh watchdog diagnostics not at zero\n");
            ++failures;
        }
    }

    // ── (8) reset() restores zero-baseline state ───────────────────
    {
        Policy policy = Policy::production();
        DeadlineWatchdog watchdog{
            /*senses=*/nullptr, policy, ::crucible::effects::Init{}};
        (void)watchdog.observe();  // observe with nullptr Senses — no state change
        watchdog.reset();
        if (watchdog.baseline_count() != 0u
            || watchdog.latest_count() != 0u
            || watchdog.window_started_ns() != 0u) {
            std::fprintf(stderr,
                "reset() should zero diagnostics\n");
            ++failures;
        }
    }

    // ── Move-construct sanity: the moved-into watchdog is usable ───
    {
        Policy policy = Policy::production();
        DeadlineWatchdog watchdog_src{
            /*senses=*/nullptr, policy, ::crucible::effects::Init{}};
        DeadlineWatchdog watchdog_sink = std::move(watchdog_src);
        const auto v = watchdog_sink.observe();
        if (v != WatchdogVerdict::InsufficientData) {
            std::fprintf(stderr,
                "moved-into watchdog should still observe; got %s\n",
                watchdog_verdict_name(v));
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("rt::DeadlineWatchdog smoke OK\n");
    }
    return failures == 0 ? 0 : 1;
}
