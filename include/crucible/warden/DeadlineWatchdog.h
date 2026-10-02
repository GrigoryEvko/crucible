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

#include <crucible/perf/Senses.h>
#include <crucible/warden/Policy.h>
#include <crucible/warden/WatchdogVerdict.h>
#include <fixy/Ctx.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Time.h>
#include <foundation/Platform.h>
#include <foundation/Saturate.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <expected>
#include <system_error>
#include <type_traits>

namespace crucible::warden {

// The verdict, WatchdogVerdict, is in crucible/warden/WatchdogVerdict.h.

// The telemetry pointer is borrowed, and what it points at must
// outlive the watchdog. It is a raw pointer rather than a non-null
// borrow type because a null source is part of the contract: a host
// without the telemetry capability, or a test exercising the degraded
// path, passes one deliberately.

// A poll belongs to a background, start-up or test context. The hot
// foreground is rejected.  A poll reads the boot clock, so the gate is
// the gate of the clock reader.
template <typename Ctx>
concept CtxFitsDeadlineWatchdog = ::foundation::effects::IsExecCtx<Ctx> && ::fixy::time::CtxFitsMonotonicClock<Ctx>;

// Building a watchdog belongs to process startup, because the watchdog
// takes the baseline that the first window is measured against.  The
// build asks for a context that owns the Init atom.  A hot foreground
// context or a background context observes a watchdog that already
// exists.
template <class Ctx>
concept CtxFitsDeadlineWatchdogMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

class DeadlineWatchdog;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsDeadlineWatchdogMint<Ctx>
[[nodiscard]] constexpr DeadlineWatchdog mint_deadline_watchdog(Ctx const&, const ::crucible::perf::Senses* senses,
                                                                const Policy& policy) noexcept;

class DeadlineWatchdog {
    // The constructor is private, and the mint is its one friend.  So a
    // watchdog comes only from a context that owns the Init atom.
    constexpr explicit DeadlineWatchdog(const ::crucible::perf::Senses* senses, const Policy& policy) noexcept
        : senses_{senses},
          miss_budget_{policy.deadline_miss_budget},
          window_ns_{static_cast<uint64_t>(policy.watchdog_window_sec) * 1000000000ull} {
        // The body is empty on purpose. The telemetry source may still
        // be loading when the watchdog is built, so the first observe()
        // captures the baseline. All-zero state is that pending
        // sentinel.
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsDeadlineWatchdogMint<Ctx>
    friend constexpr DeadlineWatchdog mint_deadline_watchdog(Ctx const&, const ::crucible::perf::Senses* senses,
                                                             const Policy& policy) noexcept;

public:
    // The clock read is the boot clock rather than the monotonic clock,
    // because a host suspend freezes the monotonic clock. The window
    // would then never close and the verdict would stay at
    // InsufficientData for as long as the host stayed asleep.
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsDeadlineWatchdog<Ctx>
    [[nodiscard]] WatchdogVerdict observe(Ctx const& ctx) noexcept {
        // The reader is new on each call, so its clamp never replaces a
        // reading.
        const auto boot_clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Boot>(ctx);
        return observe_at(ctx, boot_clock.read());
    }

    // The same step, over a boot-clock reading that the caller took.  A
    // failed read counts as no signal.  Only a reader builds a reading, so
    // a caller cannot give a time that the boot clock never returned.
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsDeadlineWatchdog<Ctx>
    [[nodiscard]] WatchdogVerdict
    observe_at(Ctx const& /*ctx*/,
               std::expected<::fixy::BootClockBytes<uint64_t>, std::error_code> const& now) noexcept {
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

        // A failed read gives no reading.  The boot clock never goes
        // back, so a reading before the start of the window is a reading
        // that the caller kept, and it counts as no signal as well.
        if (!now) [[unlikely]] {
            return WatchdogVerdict::InsufficientData;
        }
        const uint64_t now_ns = now->peek();
        if (now_ns < window_started_ns_) [[unlikely]] {
            return WatchdogVerdict::InsufficientData;
        }
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
            const uint64_t misses = ::foundation::sat::sub_sat<uint64_t>(count, baseline_count_);
            const WatchdogVerdict v = (misses > miss_budget_) ? WatchdogVerdict::Downgrade : WatchdogVerdict::Healthy;

            window_started_ns_ = now_ns;
            baseline_count_ = count;
            return v;
        }

        // A budget already blown mid-window is reported at once rather
        // than a whole window later, which matters during a storm of
        // misses.
        const uint64_t misses = ::foundation::sat::sub_sat<uint64_t>(count, baseline_count_);
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
        return ::foundation::sat::sub_sat<uint64_t>(latest_count_, baseline_count_);
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

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsDeadlineWatchdogMint<Ctx>
[[nodiscard]] constexpr DeadlineWatchdog mint_deadline_watchdog(Ctx const&, const ::crucible::perf::Senses* senses,
                                                                const Policy& policy) noexcept {
    return DeadlineWatchdog{senses, policy};
}

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
