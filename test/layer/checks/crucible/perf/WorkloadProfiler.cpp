// The compile-time checks of crucible/perf/WorkloadProfiler.h.

#include <crucible/perf/WorkloadProfiler.h>

namespace crucible::perf {

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
