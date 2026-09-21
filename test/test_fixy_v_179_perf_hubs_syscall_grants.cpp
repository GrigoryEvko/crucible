// Each perf hub carries a type-level list of the privileged syscalls its
// load path issues.  The list is an audit trail, and it is only worth
// anything while it still matches the syscalls actually made.

#include <crucible/perf/SenseHub.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/LockContention.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/fixy/syscall/Bridge.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace {

namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
namespace eff = ::crucible::effects;
namespace fxbr = ::crucible::fixy::syscall::bridge;

// The ordinals are append-only: an existing one keeps its value forever,
// so a federation cache key that hashed a syscall id never drifts.  Adding
// an enumerator anywhere but the end invalidates every such key.
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::bpf) == 41);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::perf_event_open) == 42);

static_assert(fsc::family_of(fsc::SyscallId::bpf) == fll::SyscallFamily::Privilege);
static_assert(fsc::family_of(fsc::SyscallId::perf_event_open) == fll::SyscallFamily::Privilege);

// An established classification must not shift when the catalog grows.
static_assert(fsc::family_of(fsc::SyscallId::mmap) == fll::SyscallFamily::MemoryMapping);

// Two syscalls in the same family still need separate types, because each
// gets its own federation cache slot.
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::bpf>, fsc::per<fsc::SyscallId::perf_event_open>>);
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::bpf>, fsc::per<fsc::SyscallId::prctl>>);
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::perf_event_open>, fsc::per<fsc::SyscallId::ptrace>>);

using BpfRow = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
using MmapRow = ::crucible::effects::Row<::crucible::effects::Effect::IO>;

#define FIXY_V_179_AUDIT_HUB(HubAlias)                                                                          \
    do {                                                                                                        \
        using HG = ::crucible::perf::HubAlias;                                                                  \
        static_assert(std::tuple_size_v<HG> == 3,                                                               \
                      #HubAlias " must enumerate exactly 3 grants: bpf, "                                       \
                                "perf_event_open and mmap.  Drift between the declared set and "                \
                                "the syscalls actually issued is an admission soundness "                       \
                                "regression.");                                                                 \
        static_assert(std::is_same_v<std::tuple_element_t<0, HG>, fsc::per<fsc::SyscallId::bpf>>);              \
        static_assert(std::is_same_v<std::tuple_element_t<1, HG>, fsc::per<fsc::SyscallId::perf_event_open>>);  \
        static_assert(std::is_same_v<std::tuple_element_t<2, HG>, fsc::per<fsc::SyscallId::mmap>>);             \
        static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::bpf>>, BpfRow>);   \
        static_assert(                                                                                          \
            std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::perf_event_open>>, BpfRow>); \
        static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::mmap>>, MmapRow>); \
    } while (0)

[[maybe_unused]] inline void audit_all_hubs() noexcept {
    FIXY_V_179_AUDIT_HUB(mint_sense_hub_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_pmu_sample_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_lock_contention_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_sched_switch_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_sched_tp_btf_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_syscall_tp_btf_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_syscall_latency_syscall_grants);
}

#undef FIXY_V_179_AUDIT_HUB

// A row that carries Block is the gate, because each load calls
// bpf(BPF_PROG_LOAD) and waits on the kernel verifier.  An
// initialization, background-drain or hot-foreground context is out of
// bounds for every one of these mints.  The widened background context
// below is the production shape.
using BgProbeCtx =
    eff::ExecCtx<eff::Bg, eff::ctx_numa::Local, eff::ctx_alloc::Heap, eff::ctx_heat::Cold, eff::ctx_resid::DRAM,
                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

static_assert(!::crucible::perf::CtxFitsSenseHubMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSenseHubMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSenseHubMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsSenseHubMint<BgProbeCtx>);

static_assert(!::crucible::perf::CtxFitsPmuSampleMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsPmuSampleMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsPmuSampleMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsPmuSampleMint<BgProbeCtx>);

static_assert(!::crucible::perf::CtxFitsLockContentionMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsLockContentionMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsLockContentionMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsLockContentionMint<BgProbeCtx>);

static_assert(!::crucible::perf::CtxFitsSchedSwitchMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSchedSwitchMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSchedSwitchMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsSchedSwitchMint<BgProbeCtx>);

static_assert(!::crucible::perf::CtxFitsSchedTpBtfMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSchedTpBtfMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSchedTpBtfMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsSchedTpBtfMint<BgProbeCtx>);

static_assert(!::crucible::perf::CtxFitsSyscallTpBtfMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallTpBtfMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallTpBtfMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsSyscallTpBtfMint<BgProbeCtx>);

static_assert(!::crucible::perf::CtxFitsSyscallLatencyMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallLatencyMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallLatencyMint<eff::HotFgCtx>);
static_assert(::crucible::perf::CtxFitsSyscallLatencyMint<BgProbeCtx>);

}  // namespace

int main() {
    // Every claim here is a compile-time one.  Calling these mints for
    // real needs capabilities an unprivileged test run does not have.
    return 0;
}
