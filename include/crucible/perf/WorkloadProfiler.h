#pragma once

#include <crucible/perf/SenseHub.h>
#include <crucible/perf/Senses.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <foundation/Lifetime.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::perf {

// Each threshold is a delta between two successive recommend calls, so
// the rate it stands for follows from the caller's cadence.  A caller
// that polls irregularly reads them wrong.
//
// A demotion that fires needlessly costs one parallel speedup.  A
// demotion that fails to fire costs the promise that the profiler never
// makes a workload slower.  The defaults lean towards the first, and a
// quiet host can afford higher ones.
struct WorkloadProfilerConfig {
    uint64_t futex_wait_demote_threshold = 100;
    uint64_t ctx_vol_demote_threshold = 500;
};

class WorkloadProfiler;

// A parallelism decision that came out of a profiler.  Its constructor
// is private and WorkloadProfiler is its one friend, so no caller can
// wrap a decision it wrote by hand, and the dispatch below takes nothing
// else.  A provenance tag would not do: any caller can mint a value
// under a source tag.
//
// The two assignments are user-provided, so the class is not trivially
// copyable and std::bit_cast cannot build one from bytes.  The
// annotation refuses the checked lifetime start over bytes.  The copy
// and move constructors stay trivial, so the value still travels in
// registers, and one decision can be dispatched more than once, which a
// retry path needs.
class [[nodiscard]] [[=::foundation::lifetime::no_start_over_bytes{}]] ProfiledDecision {
public:
    constexpr ProfiledDecision(const ProfiledDecision&) noexcept = default;
    constexpr ProfiledDecision(ProfiledDecision&&) noexcept = default;

    constexpr ProfiledDecision& operator=(const ProfiledDecision& other) noexcept {
        value_ = other.value_;
        return *this;
    }
    constexpr ProfiledDecision& operator=(ProfiledDecision&& other) noexcept {
        value_ = other.value_;
        return *this;
    }

    [[nodiscard]] constexpr const ::fixy::concurrent::ParallelismDecision& value() const noexcept { return value_; }

private:
    friend class WorkloadProfiler;

    constexpr explicit ProfiledDecision(::fixy::concurrent::ParallelismDecision value) noexcept : value_{value} {}

    ::fixy::concurrent::ParallelismDecision value_;
};

// Building a profiler belongs to process startup.  The profiler borrows
// a Senses that only a startup load produces, and it takes its baseline
// on its first call.  The gate reads the Init atom from the row of the
// context.  The startup load context claims Block on top of the cold
// init row and still holds Init, so both startup contexts pass, and
// every background, test and foreground context fails.
using workload_profiler_required_row = ::foundation::effects::Row<::foundation::effects::Effect::Init>;

template <class Ctx>
concept CtxFitsWorkloadProfilerMint = ::foundation::effects::CtxAdmits<Ctx, workload_profiler_required_row>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsWorkloadProfilerMint<Ctx>
[[nodiscard]] constexpr WorkloadProfiler mint_workload_profiler(Ctx const&, const Senses* senses,
                                                                WorkloadProfilerConfig cfg = {}) noexcept;

// The structural rule reasons about the working set alone and cannot
// see the live machine.  A host already saturating its scheduler makes
// parallel workers strictly worse, because every worker then fights
// the same scheduler.  This filter reads the kernel counters between
// calls and demotes a parallel recommendation to sequential when they
// say the machine is under stress.  It never promotes.
class WorkloadProfiler {
    // The constructor is private, and the mint is its one friend.
    // `senses` may be null.  The profiler then passes the structural
    // decision through and demotes nothing, which is the shape on a host
    // without CAP_BPF.
    constexpr explicit WorkloadProfiler(const Senses* senses, WorkloadProfilerConfig cfg) noexcept
        : senses_{senses}, cfg_{cfg} {}

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsWorkloadProfilerMint<Ctx>
    friend constexpr WorkloadProfiler mint_workload_profiler(Ctx const&, const Senses* senses,
                                                             WorkloadProfilerConfig cfg) noexcept;

public:
    // The result never exceeds the structural recommendation.
    // Sequential stays sequential, and parallel either stays parallel
    // or falls back to sequential.
    [[nodiscard]] ProfiledDecision recommend(::fixy::concurrent::WorkBudget budget) noexcept {
        return ProfiledDecision{recommend_bare(budget)};
    }

    // The bare form is for a caller that wants to read the decision
    // without acting on it.  Code that acts on a decision goes through
    // recommend, whose result is the only one the dispatch below takes.
    [[nodiscard]] ::fixy::concurrent::ParallelismDecision recommend_bare(::fixy::concurrent::WorkBudget budget) noexcept {
        const auto decision = ::fixy::concurrent::ParallelismRule::recommend(budget);
        if (!decision.is_parallel() || senses_ == nullptr) return pass_through_(decision);

        // A load that attached only some of its facades still yields a
        // Senses, so the hub can be absent even here.
        const auto* hub = senses_->sense_hub();
        if (hub == nullptr) return pass_through_(decision);

        const auto current = hub->read();
        if (first_call_) {
            last_ = current;
            first_call_ = false;
            return pass_through_(decision);
        }

        const auto delta = current - last_;
        last_ = current;
        futex_delta_ = delta[Idx::FUTEX_WAIT_COUNT];
        ctx_vol_delta_ = delta[Idx::SCHED_CTX_VOL];

        if (futex_delta_ > cfg_.futex_wait_demote_threshold || ctx_vol_delta_ > cfg_.ctx_vol_demote_threshold)
            [[unlikely]] {
            was_demoted_ = true;
            return ::fixy::concurrent::ParallelismDecision{
                .kind = ::fixy::concurrent::ParallelismDecision::Kind::Sequential,
                .factor = 1,
                .numa = ::fixy::concurrent::NumaPolicy::NumaIgnore,
                .tier = decision.tier,  // the structural tier stays, for diagnostics
            };
        }
        was_demoted_ = false;
        return decision;
    }

    // True when the last call went sequential because the counters
    // forced it, and false when the structural rule had already
    // chosen sequential on its own.
    [[nodiscard]] bool last_was_demoted() const noexcept { return was_demoted_; }

    [[nodiscard]] uint64_t last_futex_wait_delta() const noexcept { return futex_delta_; }
    [[nodiscard]] uint64_t last_ctx_vol_delta() const noexcept { return ctx_vol_delta_; }

    [[nodiscard]] WorkloadProfilerConfig config() const noexcept { return cfg_; }

    // Telemetry carried across a change of workload produces a delta
    // between two unrelated regions.  Reset at such a boundary so the
    // next call starts a new baseline.  last_ keeps its contents:
    // first_call_ gates every read of it, so nothing observes it before
    // the next call overwrites it.
    void reset() noexcept {
        first_call_ = true;
        clear_diagnostics_();
    }

    WorkloadProfiler(const WorkloadProfiler&) = delete("WorkloadProfiler holds a borrowed Senses*; copying would "
                                                       "produce two profilers with diverging last_ snapshots that "
                                                       "race on the same underlying SenseHub state");
    WorkloadProfiler& operator=(const WorkloadProfiler&) = delete("see copy ctor rationale");
    WorkloadProfiler(WorkloadProfiler&&) noexcept = default;
    WorkloadProfiler& operator=(WorkloadProfiler&&) noexcept = default;
    ~WorkloadProfiler() = default;

private:
    void clear_diagnostics_() noexcept {
        was_demoted_ = false;
        futex_delta_ = 0;
        ctx_vol_delta_ = 0;
    }

    // Every path that leaves the structural decision unchanged goes
    // through here, so each one clears the diagnostics of the last call.
    [[nodiscard]] ::fixy::concurrent::ParallelismDecision pass_through_(
        ::fixy::concurrent::ParallelismDecision decision) noexcept {
        clear_diagnostics_();
        return decision;
    }

    const Senses* senses_ = nullptr;
    WorkloadProfilerConfig cfg_{};
    Snapshot last_{};
    bool first_call_ = true;
    bool was_demoted_ = false;
    uint64_t futex_delta_ = 0;
    uint64_t ctx_vol_delta_ = 0;
};

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsWorkloadProfilerMint<Ctx>
[[nodiscard]] constexpr WorkloadProfiler mint_workload_profiler(Ctx const&, const Senses* senses,
                                                                WorkloadProfilerConfig cfg) noexcept {
    return WorkloadProfiler{senses, cfg};
}

// The parallel arm of a dispatch starts threads, which is background
// work, so the gate asks for the Bg atom.  The sequential arm asks for
// the same one, because the call site chooses its context before it
// knows which arm will fire.
using workload_dispatch_required_row = ::foundation::effects::Row<::foundation::effects::Effect::Bg>;

template <class Ctx>
concept CtxFitsWorkloadDecisionDispatch = ::foundation::effects::CtxAdmits<Ctx, workload_dispatch_required_row>;

template <::foundation::effects::IsExecCtx Ctx, class SeqBody, class ParBody>
    requires CtxFitsWorkloadDecisionDispatch<Ctx>
          && ::std::invocable<SeqBody&&, const ::fixy::concurrent::ParallelismDecision&>
          && ::std::invocable<ParBody&&, const ::fixy::concurrent::ParallelismDecision&>
constexpr auto dispatch_workload_decision(Ctx const&, const ProfiledDecision& decision, SeqBody&& seq_body,
                                          ParBody&& par_body)
    noexcept(::std::is_nothrow_invocable_v<SeqBody&&, const ::fixy::concurrent::ParallelismDecision&>
             && ::std::is_nothrow_invocable_v<ParBody&&, const ::fixy::concurrent::ParallelismDecision&>) {
    const auto& dec = decision.value();
    if (dec.is_parallel()) {
        return ::std::forward<ParBody>(par_body)(dec);
    }
    return ::std::forward<SeqBody>(seq_body)(dec);
}

// ---------------------------------------------------------------------
// The header proves its own claims here.

namespace detail::workload_profiler_self_test {

static_assert(CtxFitsWorkloadProfilerMint<::fixy::ColdInitCtx>);
static_assert(CtxFitsWorkloadProfilerMint<::fixy::InitLoadCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::fixy::BgLoadCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::fixy::HotFgCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsWorkloadProfilerMint<::foundation::effects::ExecCtx<>>);
static_assert(!CtxFitsWorkloadProfilerMint<int>);

static_assert(CtxFitsWorkloadDecisionDispatch<::fixy::BgDrainCtx>);
static_assert(CtxFitsWorkloadDecisionDispatch<::fixy::BgCompileCtx>);
static_assert(CtxFitsWorkloadDecisionDispatch<::fixy::BgLoadCtx>);
static_assert(!CtxFitsWorkloadDecisionDispatch<::fixy::HotFgCtx>);
static_assert(!CtxFitsWorkloadDecisionDispatch<::fixy::ColdInitCtx>);
static_assert(!CtxFitsWorkloadDecisionDispatch<::fixy::InitLoadCtx>);
static_assert(!CtxFitsWorkloadDecisionDispatch<int>);

static_assert(!std::is_constructible_v<WorkloadProfiler, const Senses*, WorkloadProfilerConfig>,
              "The constructor is private.  A profiler comes only from mint_workload_profiler.");
static_assert(!std::is_default_constructible_v<WorkloadProfiler>);

static_assert(!std::is_constructible_v<ProfiledDecision, ::fixy::concurrent::ParallelismDecision>,
              "Only a profiler builds a ProfiledDecision.");
static_assert(!std::is_default_constructible_v<ProfiledDecision>);
static_assert(!std::is_convertible_v<::fixy::concurrent::ParallelismDecision, ProfiledDecision>);
static_assert(!std::is_trivially_copyable_v<ProfiledDecision>, "std::bit_cast must not build a ProfiledDecision");
static_assert(std::is_trivially_copy_constructible_v<ProfiledDecision>,
              "The copy constructor stays trivial, so the decision travels in registers.");
static_assert(sizeof(ProfiledDecision) == sizeof(::fixy::concurrent::ParallelismDecision),
              "The proof of origin carries no storage.");

}  // namespace detail::workload_profiler_self_test

}  // namespace crucible::perf
