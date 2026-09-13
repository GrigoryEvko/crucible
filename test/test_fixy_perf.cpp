// The re-export is a using-declaration, so the claim to check is name
// lookup, not behaviour: taking the address of each template through both
// spellings must yield the same type. For a free function template that is the
// strongest reach witness available.
//
// One hub ships through a sibling umbrella instead, because the two umbrellas
// cannot be included in the same translation unit. Its entries are absent from
// every list below and are covered elsewhere.

#include <crucible/fixy/Perf.h>

#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/Senses.h>

#include <type_traits>

namespace fp = ::crucible::fixy::perf;
namespace perf_ = ::crucible::perf;
namespace eff = ::crucible::effects;

static_assert(std::is_same_v<decltype(&fp::mint_lock_contention<eff::ColdInitCtx>),
                             decltype(&perf_::mint_lock_contention<eff::ColdInitCtx>)>,
              "the re-exported mint must be the substrate function itself.");

static_assert(std::is_same_v<decltype(&fp::mint_pmu_sample<eff::ColdInitCtx>),
                             decltype(&perf_::mint_pmu_sample<eff::ColdInitCtx>)>,
              "the re-exported mint must be the substrate function itself.");

static_assert(std::is_same_v<decltype(&fp::mint_sched_switch<eff::ColdInitCtx>),
                             decltype(&perf_::mint_sched_switch<eff::ColdInitCtx>)>,
              "the re-exported mint must be the substrate function itself.");

static_assert(std::is_same_v<decltype(&fp::mint_sched_tp_btf<eff::ColdInitCtx>),
                             decltype(&perf_::mint_sched_tp_btf<eff::ColdInitCtx>)>,
              "the re-exported mint must be the substrate function itself.");

static_assert(
    std::is_same_v<decltype(&fp::mint_sense_hub<eff::ColdInitCtx>), decltype(&perf_::mint_sense_hub<eff::ColdInitCtx>)>,
    "the re-exported mint must be the substrate function itself.");

static_assert(std::is_same_v<decltype(&fp::mint_syscall_latency<eff::ColdInitCtx>),
                             decltype(&perf_::mint_syscall_latency<eff::ColdInitCtx>)>,
              "the re-exported mint must be the substrate function itself.");

static_assert(std::is_same_v<decltype(&fp::mint_syscall_tp_btf<eff::ColdInitCtx>),
                             decltype(&perf_::mint_syscall_tp_btf<eff::ColdInitCtx>)>,
              "the re-exported mint must be the substrate function itself.");

// This one has two overloads. The address is disambiguated to the three-
// argument form by casting to its signature. The four-argument form travels
// on the same using-declaration by name, so its identity follows.

using WorkloadProfiler3Arg = ::crucible::perf::WorkloadProfiler (*)(const eff::ColdInitCtx&, const perf_::Senses*,
                                                                    eff::Init) noexcept;
static_assert(
    std::is_same_v<decltype(static_cast<WorkloadProfiler3Arg>(&fp::mint_workload_profiler<eff::ColdInitCtx>)),
                   decltype(static_cast<WorkloadProfiler3Arg>(&perf_::mint_workload_profiler<eff::ColdInitCtx>))>,
    "the re-exported three-argument mint must be the substrate function "
    "itself.");

static_assert(std::is_same_v<fp::LockContention, perf_::LockContention>,
              "fixy::perf::LockContention must alias substrate.");

static_assert(std::is_same_v<fp::PmuSample, perf_::PmuSample>, "fixy::perf::PmuSample must alias substrate.");

static_assert(std::is_same_v<fp::SchedSwitch, perf_::SchedSwitch>, "fixy::perf::SchedSwitch must alias substrate.");

static_assert(std::is_same_v<fp::SchedTpBtf, perf_::SchedTpBtf>, "fixy::perf::SchedTpBtf must alias substrate.");

static_assert(std::is_same_v<fp::SenseHub, perf_::SenseHub>, "fixy::perf::SenseHub must alias substrate.");

static_assert(std::is_same_v<fp::SyscallLatency, perf_::SyscallLatency>,
              "fixy::perf::SyscallLatency must alias substrate.");

static_assert(std::is_same_v<fp::SyscallTpBtf, perf_::SyscallTpBtf>, "fixy::perf::SyscallTpBtf must alias substrate.");

static_assert(std::is_same_v<fp::WorkloadProfiler, perf_::WorkloadProfiler>,
              "fixy::perf::WorkloadProfiler must alias substrate.");

// The substrate already asserts these where each mint is defined. Repeating
// them through the re-export means a relaxed gate reddens here too, not only
// at the definition site.

static_assert(fp::CtxFitsLockContentionMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsLockContentionMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsLockContentionMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsPmuSampleMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsPmuSampleMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsPmuSampleMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSchedSwitchMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSchedSwitchMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSchedSwitchMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSchedTpBtfMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSchedTpBtfMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSchedTpBtfMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSenseHubMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSenseHubMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSenseHubMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSyscallLatencyMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSyscallLatencyMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSyscallLatencyMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSyscallTpBtfMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSyscallTpBtfMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSyscallTpBtfMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsWorkloadProfilerMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsWorkloadProfilerMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsWorkloadProfilerMint<eff::HotFgCtx>);

// A floor, not the exact count. The exact pin sits beside the constant it
// counts, where anyone changing the constant has to see it. This one catches
// the other direction: a mint removed without review. Growth past the floor is
// silent here by design.

static_assert(::crucible::fixy::perf::self_test::perf_mint_cardinality >= 8,
              "the perf mint cardinality regressed below 8, so a mint was removed "
              "without the paired exact pin being updated.");

int main() {
    // None of the mints is called. Each one issues bpf syscalls, which fail
    // in an unprivileged sandbox. The header's own smoke body is the one
    // runtime thing this target needs to reach.
    ::crucible::fixy::perf::runtime_smoke_test();
    return 0;
}
