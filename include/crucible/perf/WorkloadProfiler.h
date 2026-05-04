#pragma once

// crucible::perf::WorkloadProfiler — runtime telemetry → parallelism
// decision filter (GAPS-004h, SEPLOG-F3 #322).
//
// ─── PURPOSE ──────────────────────────────────────────────────────────
//
// `concurrent::ParallelismRule::recommend(budget)` is structurally
// correct — it picks Sequential when the working set fits in L1/L2,
// scales up Parallel when DRAM-bound.  But it doesn't see the live
// system state.  If the kernel is already context-switching the
// process furiously (futex contention from elsewhere, scheduler
// pressure from co-tenants, RT-band thrash), spawning parallel
// workers makes it strictly worse — every worker fights the same
// scheduler that's already saturating.
//
// WorkloadProfiler reads the SenseHub counters between recommend()
// calls and DEMOTES Parallel→Sequential when telemetry signals the
// system is already under stress.  It never promotes.  This extends
// the "never regress" promise from `ParallelismRule` (no regression
// on small data) to `WorkloadProfiler` (no regression under runtime
// contention either).
//
// ─── DESIGN ───────────────────────────────────────────────────────────
//
//   profiler.recommend(budget) →
//       structural_decision = ParallelismRule::recommend(budget)
//       if structural_decision.kind == Sequential: return as-is
//
//       delta = SenseHub::read() - last_snapshot
//       last_snapshot = SenseHub::read()
//
//       if delta.futex_wait_count    > FUTEX_DEMOTE_THRESHOLD:
//           return Sequential (demoted, contention)
//       if delta.sched_ctx_vol        > CTX_VOL_DEMOTE_THRESHOLD:
//           return Sequential (demoted, scheduler thrash)
//
//       return structural_decision
//
// The thresholds are per-recommend-call deltas, so callers MUST call
// recommend() at a predictable cadence (per-iteration of the Keeper
// tick) for the rates to make sense.  At ~10-100 ms cadence, the
// defaults represent ~10K futex_waits/sec and ~50K ctx_switches/sec
// — values typical of "kernel scheduler is overloaded" rather than
// normal program activity.
//
// ─── TELEMETRY-ABSENT FALLBACK ────────────────────────────────────────
//
// Construction with `senses=nullptr` is supported and useful: the
// profiler degrades to a thin pass-through over ParallelismRule.
// This is the right shape on systems without CAP_BPF, in CI runs,
// or in tests that want to assert "profiler doesn't regress when
// telemetry is unavailable."
//
// ─── USAGE ────────────────────────────────────────────────────────────
//
//     auto senses = crucible::perf::Senses::load_subset(
//         crucible::effects::Init{},
//         crucible::perf::SensesMask{ .sense_hub = true });
//     crucible::perf::WorkloadProfiler profiler{
//         &senses, crucible::effects::Init{}};
//
//     while (running) {
//         const auto budget = my_workload.budget();
//         const auto dec = profiler.recommend(budget);
//         if (dec.is_parallel()) {
//             dispatch_parallel(my_workload, dec.factor, dec.numa);
//         } else {
//             my_workload.run_inline();
//         }
//     }
//
// ─── COST ─────────────────────────────────────────────────────────────
//
//   Senses present (CAP_BPF):  ParallelismRule::recommend (~5 ns) +
//                              SenseHub::read (~30 ns mmap copy) +
//                              SenseHub::Snapshot::operator- (~80 ns
//                              for 96 sub_sat) ≈ 115 ns per call.
//
//   Senses nullptr / un-attached: ParallelismRule::recommend only,
//                                 ~5 ns.
//
// Either way, recommend() is NOT a hot-path per-event call — it's
// per-iteration / per-Keeper-tick.  Don't put it inside the inner
// loop of the workload itself; call it once before the dispatch.
//
// ─── SAFETY POSTURE ───────────────────────────────────────────────────
//
//   • InitSafe:     all fields NSDMI-initialized; first_call_ flag
//                   discriminates "no prior snapshot to delta against."
//   • TypeSafe:     thresholds are uint64_t named constants per axis;
//                   no raw integer parameters.
//   • NullSafe:     senses_ may legitimately be nullptr; every read
//                   path checks before dereference.
//   • MemSafe:      no allocations; Senses is borrowed (caller owns).
//   • ThreadSafe:   not designed for concurrent recommend() calls;
//                   the Keeper tick is single-threaded by design.
//   • LeakSafe:     no resources owned.
//   • DetSafe:      identical inputs (same prior snapshot, same budget,
//                   same SenseHub reading) → identical decision.

#include <crucible/effects/Capabilities.h>
#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/perf/Senses.h>
#include <crucible/perf/SenseHub.h>

#include <cstdint>

namespace crucible::perf {

class WorkloadProfiler {
public:
    // Per-call demote thresholds.  These are deltas observed between
    // successive recommend() calls; the rate they represent depends
    // on caller cadence.  At 10 ms cadence:
    //   FUTEX_DEMOTE_THRESHOLD = 100 → 10K futex_waits/sec
    //   CTX_VOL_DEMOTE_THRESHOLD = 500 → 50K voluntary ctx_switches/sec
    //
    // Values intentionally conservative: false-positive demotions cost
    // a parallel speedup; false-negative (parallel under contention)
    // costs the no-regression promise.  Tune higher (more permissive)
    // for clean-room systems, lower for noisy multi-tenant hosts.
    struct Config {
        uint64_t futex_wait_demote_threshold   = 100;
        uint64_t ctx_vol_demote_threshold      = 500;
    };

    // Construction.
    //
    // `senses` may be nullptr, in which case the profiler degrades
    // to a pass-through over ParallelismRule (no demotions).  When
    // non-null, the profiler reads the underlying SenseHub on every
    // recommend() call.
    //
    // `effects::Init` capability tag — same gate as every other
    // perf-tree construction.  Hot-path frames hold no Init; this
    // prevents accidental construction from a hot-call site.
    // Two-overload form (NOT a defaulted parameter) — GCC 16 rejects
    // `Config cfg = Config{}` as a default-arg because Config's NSDMIs
    // can't be evaluated before the enclosing class WorkloadProfiler is
    // complete.  Delegating constructor sidesteps it: mem-init lists
    // are parsed after the class definition.
    explicit WorkloadProfiler(
        const Senses* senses,
        ::crucible::effects::Init init) noexcept
        : WorkloadProfiler(senses, init, Config{}) {}

    explicit WorkloadProfiler(
        const Senses* senses,
        ::crucible::effects::Init,
        Config cfg) noexcept
        : senses_{senses}, cfg_{cfg} {}

    // Structurally-correct + telemetry-aware parallelism decision.
    //
    // First call captures a baseline snapshot and returns the bare
    // structural decision (no delta available to gate against).
    // Subsequent calls compare current SenseHub state against the
    // last captured snapshot and demote Parallel→Sequential if
    // either contention metric exceeds its threshold.
    //
    // The decision is monotone-down: this method only ever returns
    // a decision <= the structural recommendation.  Sequential stays
    // Sequential; Parallel may be demoted to Sequential.
    [[nodiscard]] concurrent::ParallelismDecision
    recommend(concurrent::WorkBudget budget) noexcept {
        // Always start from the structural rule.
        auto decision = concurrent::ParallelismRule::recommend(budget);

        // Sequential decisions need no telemetry adjustment, and
        // skipping the snapshot read on cache-resident workloads
        // saves ~115 ns per call.
        if (decision.kind == concurrent::ParallelismDecision::Kind::Sequential) {
            was_demoted_   = false;
            futex_delta_   = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        // No telemetry source → cannot demote, return structural.
        if (senses_ == nullptr) {
            was_demoted_   = false;
            futex_delta_   = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        // No SenseHub attached (CAP_BPF missing, etc.) → same as no
        // senses.  Senses::sense_hub() returns nullptr on partial
        // load failure for this subprogram.
        const auto* hub = senses_->sense_hub();
        if (hub == nullptr) {
            was_demoted_   = false;
            futex_delta_   = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        const auto current = hub->read();

        // First call: capture baseline, return structural decision
        // unchanged.  No delta to gate against yet.
        if (first_call_) {
            last_         = current;
            first_call_   = false;
            was_demoted_  = false;
            futex_delta_  = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        const auto delta = current - last_;
        last_ = current;

        futex_delta_   = delta[Idx::FUTEX_WAIT_COUNT];
        ctx_vol_delta_ = delta[Idx::SCHED_CTX_VOL];

        // ── Demote on contention ──────────────────────────────────
        if (futex_delta_ > cfg_.futex_wait_demote_threshold ||
            ctx_vol_delta_ > cfg_.ctx_vol_demote_threshold) [[unlikely]] {
            was_demoted_ = true;
            return concurrent::ParallelismDecision{
                .kind   = concurrent::ParallelismDecision::Kind::Sequential,
                .factor = 1,
                .numa   = concurrent::NumaPolicy::NumaIgnore,
                .tier   = decision.tier,  // preserve diagnostic tier
            };
        }

        was_demoted_ = false;
        return decision;
    }

    // ── Diagnostics ───────────────────────────────────────────────────
    //
    // Inspectors for the most recent recommend() call.  Useful for
    // logging "why did this Vigil go sequential?" and for tests that
    // assert demotion behaviour.

    // True iff the last recommend() returned Sequential as a result of
    // demotion (rather than because the structural rule already said
    // Sequential).  False on the first call, false when senses is null,
    // false when SenseHub is unattached.
    [[nodiscard]] bool last_was_demoted() const noexcept {
        return was_demoted_;
    }

    // The per-call delta values that drove the most recent decision.
    // Both zero before the second call (no delta available) and on
    // calls where senses/sense_hub were unavailable.
    [[nodiscard]] uint64_t last_futex_wait_delta() const noexcept {
        return futex_delta_;
    }
    [[nodiscard]] uint64_t last_ctx_vol_delta() const noexcept {
        return ctx_vol_delta_;
    }

    // Inspect the configured thresholds (read-only after construction).
    [[nodiscard]] Config config() const noexcept { return cfg_; }

    // Reset baseline.  After this call, the next recommend() captures
    // a fresh baseline and returns the structural decision unchanged.
    // Useful when the workload fundamentally changes (new region
    // starts) and continuity of telemetry across the boundary would
    // produce misleading deltas.
    void reset() noexcept {
        first_call_    = true;
        was_demoted_   = false;
        futex_delta_   = 0;
        ctx_vol_delta_ = 0;
        // Don't zero last_ — preserve memory layout.  first_call_ flag
        // is the discriminator.
    }

    // Move-only — owns no resources, but the senses_ pointer's
    // borrow contract (caller must keep Senses alive) makes copying
    // a footgun (two profilers reading the same Senses concurrently
    // is a benign race in principle, but the per-instance last_
    // snapshot would diverge unhelpfully).  Move keeps the borrow
    // single-rooted.
    WorkloadProfiler(const WorkloadProfiler&) =
        delete("WorkloadProfiler holds a borrowed Senses*; copying would "
               "produce two profilers with diverging last_ snapshots that "
               "race on the same underlying SenseHub state");
    WorkloadProfiler& operator=(const WorkloadProfiler&) = delete(
        "see copy ctor rationale");
    WorkloadProfiler(WorkloadProfiler&&) noexcept = default;
    WorkloadProfiler& operator=(WorkloadProfiler&&) noexcept = default;
    ~WorkloadProfiler() = default;

private:
    const Senses*  senses_     = nullptr;
    Config         cfg_{};
    Snapshot       last_{};
    bool           first_call_  = true;
    bool           was_demoted_ = false;
    uint64_t       futex_delta_   = 0;
    uint64_t       ctx_vol_delta_ = 0;
};

}  // namespace crucible::perf
