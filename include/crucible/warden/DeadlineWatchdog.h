#pragma once

// The watchdog observes a counter, diffs it across a rolling window
// and emits a verdict. It never acts on that verdict. One caller wants
// to demote the scheduling class, another wants to watch without
// touching anything, and a third wants the verdict as an input to
// something else, so the decision to act stays with the caller.
//
// It owns no thread either. A consumer that never calls it pays
// nothing, and a consumer that does calls it from a cold path.
//
// The observation degrades rather than fails: an absent or partially
// loaded telemetry source yields InsufficientData.
//
// Why a context-switch count stands in for a deadline miss. Under the
// deadline class a thread is preempted when its runtime budget is
// exhausted, when a higher-priority task arrives, or when it yields.
// The hot dispatcher issues no syscalls and does no I/O, so it never
// yields, and no other real-time task shares its control group. Every
// preempt of it is therefore a miss, which makes the context-switch
// count a tight upper bound on the real miss count, and it needs
// neither signal plumbing nor parsing of per-task kernel state. The
// same argument holds for the first-in-first-out class. Under the
// time-shared class preemption is ordinary rather than anomalous, and
// the watchdog belongs disabled.
//
// One thread only. observe() and reset() mutate the window without
// atomics, so a caller with more than one thread serializes outside.
// Do not add atomics inside.
//
// The verdict depends on wall-clock time and on system load, so it is
// not reproducible. Calling the watchdog from a context that must
// replay bit-exactly is a structural error.

#include <crucible/Platform.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/perf/Senses.h>
#include <crucible/warden/Policy.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/ClockSource.h>

#include <chrono>
#include <cstdint>
#include <ctime>

namespace crucible::warden {

// InsufficientData carries no information. A caller must not read it
// as "healthy" and must not act on it.
enum class WatchdogVerdict : uint8_t {
    InsufficientData = 0,  // nothing to observe, or the window has not closed
    Healthy = 1,  // the miss count for the closed window is within budget
    Downgrade = 2,  // the budget is exceeded; a weaker class is advised
};

[[nodiscard, gnu::const]] inline const char* watchdog_verdict_name(WatchdogVerdict v) noexcept {
    switch (v) {
        case WatchdogVerdict::InsufficientData:
            return "InsufficientData";
        case WatchdogVerdict::Healthy:
            return "Healthy";
        case WatchdogVerdict::Downgrade:
            return "Downgrade";
        default:
            return "Invalid";
    }
}

// The telemetry pointer is borrowed, and what it points at must
// outlive the watchdog. It is a raw pointer rather than a non-null
// borrow type because a null source is part of the contract: a host
// without the telemetry capability, or a test exercising the degraded
// path, passes one deliberately.

// A poll belongs to a background or start-up context. The hot
// foreground is rejected.
template <typename Ctx>
concept CtxFitsDeadlineWatchdog =
    ::crucible::effects::CtxOwnsAnyOf<Ctx, ::crucible::effects::Effect::Bg, ::crucible::effects::Effect::Init,
                                      ::crucible::effects::Effect::Test>;

class DeadlineWatchdog {
public:
    [[nodiscard]] explicit DeadlineWatchdog(const ::crucible::perf::Senses* senses, const Policy& policy,
                                            ::crucible::effects::Init) noexcept
        : senses_{senses},
          miss_budget_{policy.deadline_miss_budget},
          window_ns_{static_cast<uint64_t>(policy.watchdog_window_sec) * 1000000000ull} {
        // The body is empty on purpose. The telemetry source may still
        // be loading when the watchdog is built, so the first observe()
        // captures the baseline. All-zero state is that pending
        // sentinel.
    }

    // The clock read is CLOCK_BOOTTIME rather than CLOCK_MONOTONIC
    // because a host suspend freezes the monotonic clock. The window
    // would then never close and the verdict would stay at
    // InsufficientData for as long as the host stayed asleep.
    template <::crucible::effects::IsExecCtx Ctx>
        requires CtxFitsDeadlineWatchdog<Ctx>
    [[nodiscard]] WatchdogVerdict observe(Ctx const&) noexcept {
        // A budget of zero is the opt-out. A window of zero is also
        // treated as off: every elapsed-time test would pass trivially
        // and the verdict would come from an observation covering
        // almost no time at all.
        if (miss_budget_ == 0 || window_ns_ == 0) {
            return WatchdogVerdict::InsufficientData;
        }

        if (senses_ == nullptr) {
            return WatchdogVerdict::InsufficientData;
        }

        const ::crucible::perf::SchedSwitch* sched = senses_->sched_switch();
        if (sched == nullptr) {
            // The counter is not attached, which happens on an older
            // kernel or without the capability to load it. That is an
            // absence of signal, not evidence of health.
            return WatchdogVerdict::InsufficientData;
        }

        // The clock has been available since Linux 2.6.39, so a
        // failure here means something is deeply wrong. Report no
        // signal and let the next call retry.
        ::timespec ts{};
        if (::clock_gettime(CLOCK_BOOTTIME, &ts) != 0) [[unlikely]] {
            return WatchdogVerdict::InsufficientData;
        }
        auto now_bytes = ::crucible::safety::mint_clock_source<::crucible::safety::ClockSource_v::Boot, std::uint64_t>(
            static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec));
        const uint64_t now_ns = std::move(now_bytes).consume();
        const uint64_t count = sched->context_switches();

        if (window_started_ns_ == 0) {
            window_started_ns_ = now_ns;
            baseline_count_ = count;
            latest_count_ = count;
            return WatchdogVerdict::InsufficientData;
        }

        latest_count_ = count;

        // The verdict describes the window that just closed, and the
        // rebase below opens the next one.
        //
        // The subtraction saturates because the counter is not
        // guaranteed monotonic across a reload of the telemetry
        // source. A plain subtraction would underflow to a huge
        // positive number and manufacture a Downgrade out of nothing.
        const uint64_t elapsed_ns = now_ns - window_started_ns_;
        if (elapsed_ns >= window_ns_) {
            const uint64_t misses = ::crucible::sat::sub_sat<uint64_t>(count, baseline_count_);
            const WatchdogVerdict v = (misses > miss_budget_) ? WatchdogVerdict::Downgrade : WatchdogVerdict::Healthy;

            window_started_ns_ = now_ns;
            baseline_count_ = count;
            return v;
        }

        // A budget already blown mid-window is reported at once rather
        // than a whole window later, which matters during a storm of
        // misses.
        const uint64_t misses = ::crucible::sat::sub_sat<uint64_t>(count, baseline_count_);
        if (misses > miss_budget_) {
            // The window is deliberately left running, so every
            // further call keeps reporting Downgrade until it closes.
            // A caller that acts on the verdict calls reset() or
            // rebuilds the watchdog against the new policy.
            return WatchdogVerdict::Downgrade;
        }

        return WatchdogVerdict::InsufficientData;
    }

    // Call this after changing the scheduling class, so the next
    // window is measured against the new class rather than the old.
    void reset() noexcept {
        window_started_ns_ = 0;
        baseline_count_ = 0;
        latest_count_ = 0;
    }

    CRUCIBLE_PURE uint64_t baseline_count() const noexcept { return baseline_count_; }
    CRUCIBLE_PURE uint64_t latest_count() const noexcept { return latest_count_; }
    CRUCIBLE_PURE uint64_t window_started_ns() const noexcept { return window_started_ns_; }
    CRUCIBLE_PURE uint32_t miss_budget() const noexcept { return miss_budget_; }
    CRUCIBLE_PURE uint64_t window_ns() const noexcept { return window_ns_; }

    // Saturating, for the same reason the subtraction in observe() is.
    CRUCIBLE_PURE uint64_t misses_in_window() const noexcept {
        return ::crucible::sat::sub_sat<uint64_t>(latest_count_, baseline_count_);
    }

    DeadlineWatchdog(const DeadlineWatchdog&) =
        delete("DeadlineWatchdog owns rolling-window state — copying would shadow window with stale data");
    DeadlineWatchdog& operator=(const DeadlineWatchdog&) =
        delete("DeadlineWatchdog owns rolling-window state — copying would shadow window with stale data");
    DeadlineWatchdog(DeadlineWatchdog&&) noexcept = default;
    DeadlineWatchdog& operator=(DeadlineWatchdog&&) noexcept = default;
    ~DeadlineWatchdog() noexcept = default;

private:
    const ::crucible::perf::Senses* senses_ = nullptr;
    uint32_t miss_budget_ = 0;
    uint64_t window_ns_ = 0;
    uint64_t window_started_ns_ = 0;  // zero until the first observation
    uint64_t baseline_count_ = 0;
    uint64_t latest_count_ = 0;
};

static_assert(sizeof(DeadlineWatchdog) <= 64, "DeadlineWatchdog must fit in one cache line");

// Building a watchdog belongs to start-up, because it takes the
// baseline the first window is measured against. A hot foreground or
// background context observes one that already exists.
template <class Ctx>
concept CtxFitsDeadlineWatchdogMint = effects::IsExecCtx<Ctx> && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

template <effects::IsExecCtx Ctx>
    requires CtxFitsDeadlineWatchdogMint<Ctx>
[[nodiscard]] constexpr DeadlineWatchdog mint_deadline_watchdog(Ctx const&, const ::crucible::perf::Senses* senses,
                                                                const Policy& policy) noexcept {
    return DeadlineWatchdog{senses, policy, ::crucible::effects::Init{}};
}

static_assert(CtxFitsDeadlineWatchdogMint<effects::ColdInitCtx>);
static_assert(!CtxFitsDeadlineWatchdogMint<effects::BgDrainCtx>);
static_assert(!CtxFitsDeadlineWatchdogMint<effects::HotFgCtx>);

// The order a caller steps through after a Downgrade verdict. The
// time-shared class is the floor: below it there is nothing weaker.
// The idle class is not a weaker real-time class but a different
// thing, so it is left alone.
[[nodiscard, gnu::const]] inline SchedClass demote_one_step(SchedClass c) noexcept {
    switch (c) {
        case SchedClass::Deadline:
            return SchedClass::Fifo;
        case SchedClass::Fifo:
            return SchedClass::Other;
        case SchedClass::RoundRobin:
            return SchedClass::Other;
        case SchedClass::Other:
            return SchedClass::Other;
        case SchedClass::Batch:
            return SchedClass::Other;
        case SchedClass::Idle:
            return SchedClass::Idle;
        default:
            return SchedClass::Other;
    }
}

}  // namespace crucible::warden
