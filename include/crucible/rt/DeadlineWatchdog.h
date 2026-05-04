#pragma once

// crucible::rt::DeadlineWatchdog — pure observer/decision layer that
// wires SchedSwitch context-switch telemetry into Policy's deadline-
// miss-budget enforcement.
//
// Closes the FEEDBACK LOOP gap GAPS-004g (#1283): Policy.h declares
// `deadline_miss_budget` (rolling-window cap) and `watchdog_window_sec`
// (window length) but no consumer existed.  Hardening.h applies a
// SchedClass at process start and never reconsults — there was no
// component watching whether the chosen class was still serving its
// deadline contract.
//
// ─── DESIGN INTENT ────────────────────────────────────────────────────
//
// The watchdog is PURE LOGIC — observe a counter, diff across a rolling
// window, emit a verdict.  Actuation belongs to the caller's `apply()`
// pipeline (Hardening.h is the canonical actuator).  The split
// matters because:
//
//   • The Keeper runs its own scheduler-management loop; the bench
//     harness wants to OBSERVE without demoting; a future Augur drift
//     attribution wants to READ verdicts as a feedback signal.  All
//     three callers want different actuation, but the same observation.
//
//   • The watchdog must be cheap enough to call from any cold-path
//     hook — one Senses lookup + one steady_clock read + one budget
//     comparison.  If it owned a thread, every consumer would pay for
//     a thread; pure logic is free for non-consumers.
//
//   • The watchdog must work when Senses is partially loaded (e.g.
//     SchedSwitch attached but SyscallLatency failed) — reading the
//     facade is per-program, missing programs degrade verdict to
//     InsufficientData rather than crashing.
//
// ─── HOW IT WORKS ─────────────────────────────────────────────────────
//
// `observe()` reads `Senses::sched_switch()->context_switches()` (a
// monotonic counter scoped to the bench/Keeper target_tgid).  On the
// first call it captures (count, time) as the window baseline.  On
// subsequent calls within the same window, it returns Healthy if the
// per-window budget is not exceeded.  When the window elapses, it
// rebases.  When the budget is exceeded, it returns Downgrade — the
// caller should re-apply Policy with `hot_sched` stepped one rank
// down (Deadline → Fifo → Other).
//
// Why context_switches() as the proxy for "deadline miss":
//
//   Under SCHED_DEADLINE, the dispatcher thread runs in CBS-budgeted
//   bursts; it gets preempted exactly when (a) its runtime is
//   exhausted (kernel debits and parks), (b) a higher-priority task
//   arrives, or (c) it voluntarily yields.  For Crucible's hot
//   dispatcher there is no voluntary yield (no syscalls, no I/O on
//   hot path) and no other RT task in the same CGroup, so every
//   preempt of the dispatcher tid is structurally a miss-class
//   event.  Counting context_switches() of the dispatcher tid is
//   therefore a tight upper bound on actual misses, with the
//   advantage that it works without SIGXCPU plumbing or
//   /proc/<pid>/sched parsing.
//
//   For SCHED_FIFO, the same logic applies (FIFO threads only get
//   preempted by higher-prio FIFO/RT tasks or hardirq processing —
//   both are "miss" semantics if the dispatcher is supposed to be
//   the highest-priority RT user).  For SCHED_OTHER, the watchdog
//   should be disabled (deadline_miss_budget = 0) — preemption is
//   normal, not anomalous.
//
// ─── COST DISCIPLINE ──────────────────────────────────────────────────
//
// Per observe() call (steady state):
//   • 1 std::chrono::steady_clock::now() (~30 ns)
//   • 1 Senses::sched_switch() pointer read (~1 ns)
//   • 1 SchedSwitch::context_switches() syscall (~1 µs — bpf_map_lookup)
//   • 1 budget comparison + window-elapsed branch (~5 ns)
//
// Total per observe(): ~1 µs.  Recommended invocation cadence:
// 100ms-1s (Keeper main loop tick).  At 10 Hz, watchdog cost is
// 10 µs/sec = 0.001 % CPU.  At 1 Hz, 0.0001 %.  Truly cold path.
//
// ─── AXIOM POSTURE (per Crucible Code Guide §II) ──────────────────────
//
// • InitSafe   ✓ All fields NSDMI-defaulted; padding-free POD layout.
// • TypeSafe   ✓ effects::Init cap-tag at construction; SchedClass and
//                WatchdogVerdict are strong enums; counts are uint64_t
//                throughout (matches SchedSwitch::context_switches()).
// • NullSafe   ✓ Every senses_ deref guarded; nullptr-Senses + null
//                sched_switch() both degrade to InsufficientData.
// • MemSafe    ✓ No heap allocation; senses_ is a NON-OWNING borrow
//                (heap-owned by Keeper / bench harness elsewhere).
//                Copy-deleted with reason; move-only.
// • BorrowSafe ✓ Private state never aliased; senses_ raw pointer is
//                read-only and pointer-comparable.
// • ThreadSafe ⚠ SINGLE-THREAD ONLY.  observe() / reset() mutate the
//                rolling-window state without atomics; concurrent
//                callers race.  Callers must serialize (Keeper main
//                loop is single-threaded by construction).  If a future
//                multi-threaded consumer appears, wrap with a mutex
//                outside the watchdog — do NOT add atomics inside.
// • LeakSafe   ✓ No resources owned; trivial dtor.
// • DetSafe    ✗ NON-DETERMINISTIC by design — observe() reads
//                steady_clock::now() and a kernel BPF-map counter that
//                varies with system load.  The watchdog is an OBSERVER
//                of wall-clock reality; it is not part of the bit-exact
//                replay path.  Calling it from a deterministic context
//                (e.g. replay verifier) is a structural bug.
//
// ─── HS14 NEG-COMPILE FIXTURES ────────────────────────────────────────
//
// • neg_rt_deadline_watchdog_no_cap.cpp     — construct without Init
// • neg_rt_deadline_watchdog_wrong_cap.cpp  — construct with Bg{}
//
// Same Init-by-value gate as every Senses-touching surface in the
// GAPS-004 series.

#include <crucible/Platform.h>              // CRUCIBLE_PURE for getters
#include <crucible/effects/Capabilities.h>  // effects::Init capability tag
#include <crucible/perf/Senses.h>           // Senses + SchedSwitch facade
#include <crucible/rt/Policy.h>             // Policy + SchedClass + budget
#include <crucible/safety/Checked.h>        // crucible::sat::sub_sat

#include <chrono>
#include <cstdint>

namespace crucible::rt {

// ─── WatchdogVerdict ──────────────────────────────────────────────────
//
// Three states.  InsufficientData fires when the watchdog can't
// observe (Senses missing, SchedSwitch unattached, window not yet
// elapsed) — the caller should not act on this verdict.
//
// Healthy fires when the rolling window has elapsed and the count
// of preempts in this window is at or below `deadline_miss_budget`.
//
// Downgrade fires when the count has exceeded the budget.  The caller
// is expected to re-apply Policy with `hot_sched` stepped one rank
// down.  The watchdog itself does NOT actuate the downgrade — it
// reports the verdict; the Keeper / bench harness chooses whether
// and when to act.

enum class WatchdogVerdict : uint8_t {
    InsufficientData = 0,  // can't observe (no Senses, no SchedSwitch, window not elapsed)
    Healthy          = 1,  // miss count within budget for this window
    Downgrade        = 2,  // budget exceeded; recommend SchedClass demotion
};

// Stable string for diagnostic / Augur logging.  No allocation.
[[nodiscard, gnu::const]] inline const char*
watchdog_verdict_name(WatchdogVerdict v) noexcept {
    switch (v) {
        case WatchdogVerdict::InsufficientData: return "InsufficientData";
        case WatchdogVerdict::Healthy:          return "Healthy";
        case WatchdogVerdict::Downgrade:        return "Downgrade";
        default:                                return "Invalid";
    }
}

// ─── DeadlineWatchdog ─────────────────────────────────────────────────
//
// Construction takes `effects::Init` — building the watchdog is a
// startup-only act (it captures the baseline for the first window).
// Subsequent `observe()` calls are cold-path-callable from any
// context (single-thread; see A6 above).
//
// ── Borrow contract for `senses_` ─────────────────────────────────
// The Senses pointer is a NON-OWNING borrow.  The lifetime contract
// is: the Senses instance outlives the DeadlineWatchdog.  This is not
// expressible via `safety::BorrowedRef<const Senses>` because the
// nullptr case is part of the watchdog's degraded-mode contract
// (callers without libbpf, unit tests, bench harness on a kernel
// missing CAP_BPF) — BorrowedRef is non-null by design, and wrapping
// in `Optional<BorrowedRef<...>>` adds a layer without eliminating a
// bug class the existing nullptr guards don't already prevent.
// Documented contract instead of typed contract — this matches the
// rt/Hardening.h convention for borrowed Linux-syscall state.

class DeadlineWatchdog {
public:
    [[nodiscard]] explicit DeadlineWatchdog(
        const ::crucible::perf::Senses* senses,
        const Policy& policy,
        ::crucible::effects::Init) noexcept
        : senses_{senses}
        , miss_budget_{policy.deadline_miss_budget}
        , window_ns_{static_cast<uint64_t>(policy.watchdog_window_sec)
                     * 1'000'000'000ull}
    {
        // Baseline captured lazily on first observe() — at construction
        // time we may not yet have a SchedSwitch counter to read (load
        // can race with Watchdog construction in some Keeper init
        // sequences).  All-zero state is the "first observation
        // pending" sentinel.
    }

    // Observe the current preempt count, advance the rolling window
    // if window_ns has elapsed since the last reset, and emit a
    // verdict.  Cheap; safe to call from any cold path.
    [[nodiscard]] WatchdogVerdict observe() noexcept {
        // Disabled by configuration: budget = 0 OR window = 0 both
        // mean "don't watch".  Budget 0 is the documented opt-out;
        // window 0 was a latent BUG before the audit-2 sweep — every
        // observe() saw `elapsed_ns >= window_ns_` (== 0) trivially
        // true on every call after the first, rebasing the window and
        // emitting a verdict against a sub-microsecond observation.
        // A custom Policy with `watchdog_window_sec = 0` would have
        // produced random Downgrade verdicts.  Both knobs guarded as
        // disabled-when-zero now.
        if (miss_budget_ == 0 || window_ns_ == 0) {
            return WatchdogVerdict::InsufficientData;
        }

        // No Senses → can't observe.  Also handles tests that pass
        // nullptr deliberately to exercise the InsufficientData path.
        if (senses_ == nullptr) {
            return WatchdogVerdict::InsufficientData;
        }

        const ::crucible::perf::SchedSwitch* sched = senses_->sched_switch();
        if (sched == nullptr) {
            // SchedSwitch failed to load (kernel too old, missing CAP,
            // libbpf load error).  The watchdog can't observe — caller
            // should treat as "no signal", NOT as "Healthy".
            return WatchdogVerdict::InsufficientData;
        }

        const uint64_t now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        const uint64_t count = sched->context_switches();

        // First observation — capture baseline, return InsufficientData
        // until at least one window has elapsed.
        if (window_started_ns_ == 0) {
            window_started_ns_ = now_ns;
            baseline_count_    = count;
            latest_count_      = count;
            return WatchdogVerdict::InsufficientData;
        }

        latest_count_ = count;

        // Window elapsed → rebase, return verdict for THIS window
        // before resetting.  The verdict is computed on the just-
        // closed window; the next observe() starts a fresh window.
        //
        // sub_sat (audit-2 fix): a non-monotonic counter snapshot
        // (e.g. SchedSwitch was unloaded and reloaded mid-watchdog,
        // a documented borrow-contract violation but tolerable as
        // "no signal" rather than "phantom Downgrade") would have
        // underflowed `count - baseline_count_` to a giant positive
        // and triggered a false Downgrade.  Saturate to 0 instead;
        // a degenerate baseline produces InsufficientData on the
        // next observe() once it rebases.
        const uint64_t elapsed_ns = now_ns - window_started_ns_;
        if (elapsed_ns >= window_ns_) {
            const uint64_t misses = ::crucible::sat::sub_sat<uint64_t>(
                count, baseline_count_);
            const WatchdogVerdict v = (misses > miss_budget_)
                ? WatchdogVerdict::Downgrade
                : WatchdogVerdict::Healthy;

            window_started_ns_ = now_ns;
            baseline_count_    = count;
            return v;
        }

        // Window not yet elapsed — early-warning check: if budget
        // already exceeded mid-window, signal Downgrade immediately
        // rather than waiting for the window to close.  Avoids a
        // window-length lag in pathological miss storms.  Same
        // sub_sat rationale as above.
        const uint64_t misses = ::crucible::sat::sub_sat<uint64_t>(
            count, baseline_count_);
        if (misses > miss_budget_) {
            // Don't reset the window — the next observe() will see
            // the exceeded state again until the window naturally
            // closes.  Caller is expected to demote and re-construct
            // the watchdog with the new Policy (or call reset()).
            return WatchdogVerdict::Downgrade;
        }

        return WatchdogVerdict::InsufficientData;
    }

    // Reset the rolling window to start now.  Call after demoting the
    // SchedClass so the next observe() starts a fresh window against
    // the demoted class's expected miss profile.
    void reset() noexcept {
        window_started_ns_ = 0;
        baseline_count_    = 0;
        latest_count_      = 0;
    }

    // ── Diagnostics / Augur feedback ────────────────────────────────
    //
    // All zero on a freshly-constructed or just-reset watchdog.
    // Augur reads these to attribute drift events to the SchedSwitch
    // signal.

    // CRUCIBLE_PURE = [[gnu::pure, nodiscard]] — these getters depend
    // only on member state; the optimizer can CSE redundant calls
    // (e.g. an Augur loop that reads baseline_count + latest_count
    // back-to-back compiles to two MOV reads, no aliasing assumed).
    CRUCIBLE_PURE uint64_t baseline_count() const noexcept     { return baseline_count_; }
    CRUCIBLE_PURE uint64_t latest_count() const noexcept       { return latest_count_; }
    CRUCIBLE_PURE uint64_t window_started_ns() const noexcept  { return window_started_ns_; }
    CRUCIBLE_PURE uint32_t miss_budget() const noexcept        { return miss_budget_; }
    CRUCIBLE_PURE uint64_t window_ns() const noexcept          { return window_ns_; }

    // Misses observed since window start.  `crucible::sat::sub_sat`
    // saturates to zero on the rare counter-reset edge case (e.g.
    // SchedSwitch reload between observations) — the open-coded
    // ternary previously here was equivalent but didn't grep-locate
    // alongside Crucible's other saturation sites.
    CRUCIBLE_PURE uint64_t misses_in_window() const noexcept {
        return ::crucible::sat::sub_sat<uint64_t>(latest_count_, baseline_count_);
    }

    // Move-only — no shared mutable state, but we want move semantics
    // for emplacing into Keeper state structs without copy elision
    // ambiguity.  Copy is deleted to surface accidental aliasing of
    // the rolling-window state at compile time.
    DeadlineWatchdog(const DeadlineWatchdog&) =
        delete("DeadlineWatchdog owns rolling-window state — copying would shadow window with stale data");
    DeadlineWatchdog& operator=(const DeadlineWatchdog&) =
        delete("DeadlineWatchdog owns rolling-window state — copying would shadow window with stale data");
    DeadlineWatchdog(DeadlineWatchdog&&) noexcept            = default;
    DeadlineWatchdog& operator=(DeadlineWatchdog&&) noexcept = default;
    ~DeadlineWatchdog() noexcept                             = default;

private:
    const ::crucible::perf::Senses* senses_ = nullptr;
    uint32_t miss_budget_                   = 0;
    uint64_t window_ns_                     = 0;
    uint64_t window_started_ns_             = 0;  // 0 = first observation pending
    uint64_t baseline_count_                = 0;
    uint64_t latest_count_                  = 0;
};

// Tiny: 1 ptr + 1 u32 + 4 u64 = 40 bytes.  Stack-allocatable in any
// Keeper / bench frame.  No virtual, no heap.
static_assert(sizeof(DeadlineWatchdog) <= 64,
    "DeadlineWatchdog must fit in one cache line");

// Recommended demotion table.  When `observe() == Downgrade`, the
// caller picks the next class via this helper rather than open-coding
// the order.  Stops at SchedClass::Other — once we're already on the
// time-shared class, further demotion is meaningless (Idle is a
// different semantic, not a "weaker" RT class).
[[nodiscard, gnu::const]] inline SchedClass
demote_one_step(SchedClass c) noexcept {
    switch (c) {
        case SchedClass::Deadline:   return SchedClass::Fifo;
        case SchedClass::Fifo:       return SchedClass::Other;
        case SchedClass::RoundRobin: return SchedClass::Other;
        case SchedClass::Other:      return SchedClass::Other;  // already at floor
        case SchedClass::Batch:      return SchedClass::Other;  // batch → other (same-tier downshift)
        case SchedClass::Idle:       return SchedClass::Idle;   // Idle is unrelated to RT — preserve
        default:                     return SchedClass::Other;
    }
}

} // namespace crucible::rt
