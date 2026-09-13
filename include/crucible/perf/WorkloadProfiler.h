#pragma once

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/perf/Senses.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <utility>

namespace crucible::perf {

// A bare decision can be written by hand anywhere.  The phantom tag
// records that this one came out of a profiler, and the dispatch
// below admits nothing else, so the profiler cannot be bypassed.
using TaggedParallelismDecision = ::crucible::safety::Tagged<::crucible::concurrent::ParallelismDecision,
                                                             ::crucible::safety::source::WorkloadProfiler>;

// The structural rule reasons about the working set alone and cannot
// see the live machine.  A host already saturating its scheduler makes
// parallel workers strictly worse, because every worker then fights
// the same scheduler.  This filter reads the kernel counters between
// calls and demotes a parallel recommendation to sequential when they
// say the machine is under stress.  It never promotes.
class WorkloadProfiler {
public:
    // Each threshold is a delta between two successive recommend
    // calls, so the rate it stands for follows from the caller's
    // cadence.  A caller that polls irregularly reads them wrong.
    //
    // A demotion that fires needlessly costs one parallel speedup.  A
    // demotion that fails to fire costs the promise that this filter
    // never makes a workload slower.  The defaults lean towards the
    // first, and a quiet host can afford higher ones.
    struct Config {
        uint64_t futex_wait_demote_threshold = 100;
        uint64_t ctx_vol_demote_threshold = 500;
    };

    // `senses` may be null.  The profiler then passes the structural
    // decision through and demotes nothing, which is the shape on a
    // host without CAP_BPF.
    //
    // Two overloads rather than one with `Config cfg = Config{}`.  A
    // default argument there is rejected, because the NSDMIs of Config
    // cannot be evaluated while the enclosing class is incomplete.  A
    // delegating constructor avoids that: a member-initializer list is
    // parsed after the class definition.
    explicit WorkloadProfiler(const Senses* senses, ::crucible::effects::Init init) noexcept
        : WorkloadProfiler(senses, init, Config{}) {}

    explicit WorkloadProfiler(const Senses* senses, ::crucible::effects::Init, Config cfg) noexcept
        : senses_{senses}, cfg_{cfg} {}

    // The result never exceeds the structural recommendation.
    // Sequential stays sequential, and parallel either stays parallel
    // or falls back to sequential.
    [[nodiscard]] TaggedParallelismDecision recommend(concurrent::WorkBudget budget) noexcept {
        return TaggedParallelismDecision{recommend_raw_(budget)};
    }

    // The untagged form is for a caller that wants to read the
    // decision without acting on it.  Code that acts on a decision
    // goes through the tagged form, which is the only one the dispatch
    // below accepts.
    [[nodiscard]] concurrent::ParallelismDecision recommend_bare(concurrent::WorkBudget budget) noexcept {
        return recommend_raw_(budget);
    }

    // True when the last call went sequential because the counters
    // forced it, and false when the structural rule had already
    // chosen sequential on its own.
    [[nodiscard]] bool last_was_demoted() const noexcept { return was_demoted_; }

    [[nodiscard]] uint64_t last_futex_wait_delta() const noexcept { return futex_delta_; }
    [[nodiscard]] uint64_t last_ctx_vol_delta() const noexcept { return ctx_vol_delta_; }

    [[nodiscard]] Config config() const noexcept { return cfg_; }

    // Telemetry carried across a change of workload produces a delta
    // between two unrelated regions.  Reset at such a boundary so the
    // next call starts a new baseline.
    void reset() noexcept {
        first_call_ = true;
        was_demoted_ = false;
        futex_delta_ = 0;
        ctx_vol_delta_ = 0;
        // last_ keeps its contents.  first_call_ gates every read of
        // it, so nothing observes it before the next call overwrites
        // it.
    }

    WorkloadProfiler(const WorkloadProfiler&) = delete("WorkloadProfiler holds a borrowed Senses*; copying would "
                                                       "produce two profilers with diverging last_ snapshots that "
                                                       "race on the same underlying SenseHub state");
    WorkloadProfiler& operator=(const WorkloadProfiler&) = delete("see copy ctor rationale");
    WorkloadProfiler(WorkloadProfiler&&) noexcept = default;
    WorkloadProfiler& operator=(WorkloadProfiler&&) noexcept = default;
    ~WorkloadProfiler() = default;

private:
    [[nodiscard]] concurrent::ParallelismDecision recommend_raw_(concurrent::WorkBudget budget) noexcept {
        auto decision = concurrent::ParallelismRule::recommend(budget);

        if (decision.kind == concurrent::ParallelismDecision::Kind::Sequential) {
            was_demoted_ = false;
            futex_delta_ = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        if (senses_ == nullptr) {
            was_demoted_ = false;
            futex_delta_ = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        // A load that attached only some of its facades still yields a
        // Senses, so the hub can be absent even here.
        const auto* hub = senses_->sense_hub();
        if (hub == nullptr) {
            was_demoted_ = false;
            futex_delta_ = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        const auto current = hub->read();

        if (first_call_) {
            last_ = current;
            first_call_ = false;
            was_demoted_ = false;
            futex_delta_ = 0;
            ctx_vol_delta_ = 0;
            return decision;
        }

        const auto delta = current - last_;
        last_ = current;

        futex_delta_ = delta[Idx::FUTEX_WAIT_COUNT];
        ctx_vol_delta_ = delta[Idx::SCHED_CTX_VOL];

        if (futex_delta_ > cfg_.futex_wait_demote_threshold || ctx_vol_delta_ > cfg_.ctx_vol_demote_threshold)
            [[unlikely]] {
            was_demoted_ = true;
            return concurrent::ParallelismDecision{
                .kind = concurrent::ParallelismDecision::Kind::Sequential,
                .factor = 1,
                .numa = concurrent::NumaPolicy::NumaIgnore,
                .tier = decision.tier,  // the structural tier stays, for diagnostics
            };
        }

        was_demoted_ = false;
        return decision;
    }

    const Senses* senses_ = nullptr;
    Config cfg_{};
    Snapshot last_{};
    bool first_call_ = true;
    bool was_demoted_ = false;
    uint64_t futex_delta_ = 0;
    uint64_t ctx_vol_delta_ = 0;
};

// The profiler borrows a Senses, which exists only after a startup
// load, so only a context carrying the Init capability may construct
// one.
template <class Ctx>
concept CtxFitsWorkloadProfilerMint = ::crucible::effects::IsExecCtx<Ctx>
                                   && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Init>;

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsWorkloadProfilerMint<Ctx>
// §XXI carve-out: cx=alloc — the borrowed Senses reaches this factory
// only through a BPF load, a perf_event_open and an mmap.  Compile-
// time evaluation would lie about the runtime cost.
[[nodiscard]] inline WorkloadProfiler mint_workload_profiler(Ctx const&, const Senses* senses,
                                                             ::crucible::effects::Init init) noexcept {
    return WorkloadProfiler{senses, init};
}

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsWorkloadProfilerMint<Ctx>
// §XXI carve-out: cx=alloc — as for the overload above.
[[nodiscard]] inline WorkloadProfiler mint_workload_profiler(Ctx const&, const Senses* senses,
                                                             ::crucible::effects::Init init,
                                                             WorkloadProfiler::Config cfg) noexcept {
    return WorkloadProfiler{senses, init, cfg};
}

static_assert(CtxFitsWorkloadProfilerMint<::crucible::effects::ColdInitCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::crucible::effects::BgDrainCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::crucible::effects::HotFgCtx>);

// The decision arrives by value.  It proves where it came from and is
// not an owned resource, so one decision can be dispatched more than
// once, which a retry path needs.
//
// The parallel arm starts threads, which is background work, so the
// gate asks for the Bg capability.  The sequential arm asks for the
// same one, because the call site chooses its context before it knows
// which arm will fire.
template <class Ctx>
concept CtxFitsWorkloadDecisionDispatch =
    ::crucible::effects::IsExecCtx<Ctx> && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>;

template <::crucible::effects::IsExecCtx Ctx, class SeqBody, class ParBody>
    requires CtxFitsWorkloadDecisionDispatch<Ctx> && ::std::invocable<SeqBody&&, concurrent::ParallelismDecision>
          && ::std::invocable<ParBody&&, concurrent::ParallelismDecision>
constexpr auto dispatch_workload_decision(
    Ctx const&, TaggedParallelismDecision decision, SeqBody&& seq_body,
    ParBody&& par_body) noexcept(::std::is_nothrow_invocable_v<SeqBody&&, concurrent::ParallelismDecision>
                                 && ::std::is_nothrow_invocable_v<ParBody&&, concurrent::ParallelismDecision>) {
    const auto& dec = decision.value();
    if (dec.is_parallel()) {
        return ::std::forward<ParBody>(par_body)(dec);
    }
    return ::std::forward<SeqBody>(seq_body)(dec);
}

static_assert(CtxFitsWorkloadDecisionDispatch<::crucible::effects::BgDrainCtx>);
static_assert(CtxFitsWorkloadDecisionDispatch<::crucible::effects::BgCompileCtx>);
static_assert(!CtxFitsWorkloadDecisionDispatch<::crucible::effects::HotFgCtx>);
static_assert(!CtxFitsWorkloadDecisionDispatch<::crucible::effects::ColdInitCtx>);

}  // namespace crucible::perf
