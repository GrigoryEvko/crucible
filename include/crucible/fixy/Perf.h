#pragma once

// The mint factories below live in crucible::perf.  The re-export gives a
// caller that pulls in only the fixy surface an entry point that does not
// name that namespace.
//
// The v2 sense hub is reached through a separate sub-umbrella instead.  The
// v1 and v2 hubs are alternative builds of one surface and share identifiers
// in crucible::perf, so a translation unit that included both would hit
// one-definition and enum-shape collisions.  A caller includes one umbrella
// or the other, never both.
//
// Every mint here carries an Init-row gate.  Standing a hub up loads an eBPF
// program and attaches it, both kernel-mediated mutations that belong to
// startup.  A hot foreground or background-drain context reads from a hub
// that Init-time code already minted.

#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <crucible/perf/WorkloadProfiler.h>
// The v2 hub header is deliberately absent.  See the note above.

#include <type_traits>

namespace crucible::fixy::perf {

using ::crucible::perf::mint_lock_contention;
using ::crucible::perf::CtxFitsLockContentionMint;
using ::crucible::perf::LockContention;

using ::crucible::perf::mint_pmu_sample;
using ::crucible::perf::CtxFitsPmuSampleMint;
using ::crucible::perf::PmuSample;

using ::crucible::perf::mint_sched_switch;
using ::crucible::perf::CtxFitsSchedSwitchMint;
using ::crucible::perf::SchedSwitch;

using ::crucible::perf::mint_sched_tp_btf;
using ::crucible::perf::CtxFitsSchedTpBtfMint;
using ::crucible::perf::SchedTpBtf;

using ::crucible::perf::mint_sense_hub;
using ::crucible::perf::CtxFitsSenseHubMint;
using ::crucible::perf::SenseHub;

using ::crucible::perf::mint_syscall_latency;
using ::crucible::perf::CtxFitsSyscallLatencyMint;
using ::crucible::perf::SyscallLatency;

using ::crucible::perf::mint_syscall_tp_btf;
using ::crucible::perf::CtxFitsSyscallTpBtfMint;
using ::crucible::perf::SyscallTpBtf;

using ::crucible::perf::mint_workload_profiler;
using ::crucible::perf::CtxFitsWorkloadProfilerMint;
using ::crucible::perf::WorkloadProfiler;

using ::crucible::perf::TaggedParallelismDecision;
using ::crucible::perf::CtxFitsWorkloadDecisionDispatch;
using ::crucible::perf::dispatch_workload_decision;

}  // namespace crucible::fixy::perf

// The sentinels below verify that each alias resolves to the substrate
// entity and not to a local of the same name.  Being in the header, they
// fire at every consumer's include time.

namespace crucible::fixy::perf::self_test {

static_assert(std::is_same_v<::crucible::fixy::perf::LockContention, ::crucible::perf::LockContention>,
              "fixy::perf::LockContention must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::PmuSample, ::crucible::perf::PmuSample>,
              "fixy::perf::PmuSample must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::SchedSwitch, ::crucible::perf::SchedSwitch>,
              "fixy::perf::SchedSwitch must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::SchedTpBtf, ::crucible::perf::SchedTpBtf>,
              "fixy::perf::SchedTpBtf must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::SenseHub, ::crucible::perf::SenseHub>,
              "fixy::perf::SenseHub must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::SyscallLatency, ::crucible::perf::SyscallLatency>,
              "fixy::perf::SyscallLatency must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::SyscallTpBtf, ::crucible::perf::SyscallTpBtf>,
              "fixy::perf::SyscallTpBtf must alias substrate.");

static_assert(std::is_same_v<::crucible::fixy::perf::WorkloadProfiler, ::crucible::perf::WorkloadProfiler>,
              "fixy::perf::WorkloadProfiler must alias substrate.");

static_assert(
    std::is_same_v<::crucible::fixy::perf::TaggedParallelismDecision, ::crucible::perf::TaggedParallelismDecision>,
    "fixy::perf::TaggedParallelismDecision must alias substrate.");

// The seven BPF hubs gate on a row that carries Block, because each
// load calls bpf(BPF_PROG_LOAD) and waits on the kernel verifier.  The
// initialization capability permits Row<Init, Alloc, IO> and no Block,
// so it cannot reach any of the seven.  The test runner capability
// permits Block and does reach them.

static_assert(!::crucible::fixy::perf::CtxFitsLockContentionMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsLockContentionMint must reject ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsPmuSampleMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsPmuSampleMint must reject ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedSwitchMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsSchedSwitchMint must reject ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedTpBtfMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsSchedTpBtfMint must reject ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSenseHubMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsSenseHubMint must reject ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallLatencyMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsSyscallLatencyMint must reject ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsSyscallTpBtfMint must reject ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsLockContentionMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsLockContentionMint must admit TestRunnerCtx.");

static_assert(::crucible::fixy::perf::CtxFitsPmuSampleMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsPmuSampleMint must admit TestRunnerCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSchedSwitchMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsSchedSwitchMint must admit TestRunnerCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSchedTpBtfMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsSchedTpBtfMint must admit TestRunnerCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSenseHubMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsSenseHubMint must admit TestRunnerCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSyscallLatencyMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsSyscallLatencyMint must admit TestRunnerCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<::crucible::effects::TestRunnerCtx>,
              "fixy::perf::CtxFitsSyscallTpBtfMint must admit TestRunnerCtx.");

// The profiler mint is not one of the seven.  Its constructor borrows an
// already-loaded Senses and enters no kernel of its own, so it keeps the
// initialization gate.

static_assert(::crucible::fixy::perf::CtxFitsWorkloadProfilerMint<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsWorkloadProfilerMint must admit ColdInitCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsLockContentionMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsLockContentionMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsPmuSampleMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsPmuSampleMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedSwitchMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsSchedSwitchMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedTpBtfMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsSchedTpBtfMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSenseHubMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsSenseHubMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallLatencyMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsSyscallLatencyMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsSyscallTpBtfMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsWorkloadProfilerMint<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsWorkloadProfilerMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsLockContentionMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsLockContentionMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsPmuSampleMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsPmuSampleMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedSwitchMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsSchedSwitchMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedTpBtfMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsSchedTpBtfMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSenseHubMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsSenseHubMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallLatencyMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsSyscallLatencyMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsSyscallTpBtfMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsWorkloadProfilerMint<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsWorkloadProfilerMint must reject HotFgCtx.");

// The dispatch gate is orthogonal to the mint gate above.  Minting a
// profiler happens once at startup.  Routing its decisions happens every
// iteration on a background context, and never on the foreground.

static_assert(::crucible::fixy::perf::CtxFitsWorkloadDecisionDispatch<::crucible::effects::BgDrainCtx>,
              "fixy::perf::CtxFitsWorkloadDecisionDispatch must admit BgDrainCtx.");

static_assert(::crucible::fixy::perf::CtxFitsWorkloadDecisionDispatch<::crucible::effects::BgCompileCtx>,
              "fixy::perf::CtxFitsWorkloadDecisionDispatch must admit BgCompileCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsWorkloadDecisionDispatch<::crucible::effects::HotFgCtx>,
              "fixy::perf::CtxFitsWorkloadDecisionDispatch must reject HotFgCtx "
              "(empty Row<> — a profiler decision never reaches the foreground).");

static_assert(!::crucible::fixy::perf::CtxFitsWorkloadDecisionDispatch<::crucible::effects::ColdInitCtx>,
              "fixy::perf::CtxFitsWorkloadDecisionDispatch must reject ColdInitCtx "
              "(Init row carries no Bg).");

// The exact pin sits next to the constant so a contributor bumping one
// cannot miss the other.  A separate test holds only a lower bound, which
// catches the opposite direction: a mint removed without review.

inline constexpr int perf_mint_cardinality = 8;

static_assert(perf_mint_cardinality == 8, "ceiling: fixy::perf:: re-exports exactly 8 v1 mint factories — "
                                          "mint_lock_contention, mint_pmu_sample, mint_sched_switch, "
                                          "mint_sched_tp_btf, mint_sense_hub, mint_syscall_latency, "
                                          "mint_syscall_tp_btf, mint_workload_profiler.  mint_sense_hub_v2 "
                                          "lives in fixy::perf::v2:: instead.  Adding or removing a v1 "
                                          "mint updates both the constant and this pin in one edit.");

}  // namespace crucible::fixy::perf::self_test

// The smoke test stays type-level.  Calling any of these mints would issue
// a real bpf() syscall, which a sandboxed build cannot do.

namespace crucible::fixy::perf {

inline void runtime_smoke_test() noexcept {
    constexpr bool rejects_cold = !CtxFitsSenseHubMint<::crucible::effects::ColdInitCtx>;
    constexpr bool rejects_bg = !CtxFitsPmuSampleMint<::crucible::effects::BgDrainCtx>;
    constexpr bool rejects_hot = !CtxFitsLockContentionMint<::crucible::effects::HotFgCtx>;
    constexpr bool admits_test = CtxFitsSenseHubMint<::crucible::effects::TestRunnerCtx>;
    (void)rejects_cold;
    (void)rejects_bg;
    (void)rejects_hot;
    (void)admits_test;

    constexpr bool dispatch_admits_bg = CtxFitsWorkloadDecisionDispatch<::crucible::effects::BgDrainCtx>;
    constexpr bool dispatch_rejects_hot = !CtxFitsWorkloadDecisionDispatch<::crucible::effects::HotFgCtx>;
    (void)dispatch_admits_bg;
    (void)dispatch_rejects_hot;
}

}  // namespace crucible::fixy::perf
