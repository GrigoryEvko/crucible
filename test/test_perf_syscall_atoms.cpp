// Each perf hub carries a type-level list of the privileged syscalls its
// load path issues.  The list is an audit trail, and it is only worth
// anything while it still matches the syscalls actually made.

#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

namespace sc = ::fixy::atom::syscall;
namespace fe = ::foundation::effects;
namespace eff = ::crucible::effects;

// The ordinals are append-only: an existing one keeps its value forever,
// so a federation cache key that hashed a syscall id never drifts.
static_assert(std::to_underlying(sc::SyscallId::bpf) == 41);
static_assert(std::to_underlying(sc::SyscallId::perf_event_open) == 42);

static_assert(sc::per<sc::SyscallId::bpf>::family == sc::SyscallFamily::Privilege);
static_assert(sc::per<sc::SyscallId::perf_event_open>::family == sc::SyscallFamily::Privilege);
static_assert(sc::per<sc::SyscallId::mmap>::family == sc::SyscallFamily::MemoryMapping);

// Two syscalls in the same family still need separate types, because each
// gets its own federation cache slot.
static_assert(!std::is_same_v<sc::per<sc::SyscallId::bpf>, sc::per<sc::SyscallId::perf_event_open>>);
static_assert(!std::is_same_v<sc::per<sc::SyscallId::bpf>, sc::per<sc::SyscallId::prctl>>);
static_assert(!std::is_same_v<sc::per<sc::SyscallId::perf_event_open>, sc::per<sc::SyscallId::ptrace>>);

// A privileged call and a mapping call both lift to IO and Block;
// fixy/atoms/Syscall.h says why the mapping row carries Block.
using PrivilegeRow = fe::Row<fe::Effect::IO, fe::Effect::Block>;
using MemoryMappingRow = fe::Row<fe::Effect::IO, fe::Effect::Block>;

// Every hub names the same three calls in the same order.
template <class HubAtoms>
[[nodiscard]] consteval bool names_bpf_perf_event_open_and_mmap_() noexcept {
    // A && chain instantiates every element type, so a shorter list must
    // leave before the chain names an index it does not have.
    if constexpr (std::tuple_size_v<HubAtoms> != 3) {
        return false;
    } else {
        return std::is_same_v<std::tuple_element_t<0, HubAtoms>, sc::per<sc::SyscallId::bpf>>
            && std::is_same_v<std::tuple_element_t<1, HubAtoms>, sc::per<sc::SyscallId::perf_event_open>>
            && std::is_same_v<std::tuple_element_t<2, HubAtoms>, sc::per<sc::SyscallId::mmap>>
            && std::is_same_v<fe::lift_row_t<std::tuple_element_t<0, HubAtoms>>, PrivilegeRow>
            && std::is_same_v<fe::lift_row_t<std::tuple_element_t<1, HubAtoms>>, PrivilegeRow>
            && std::is_same_v<fe::lift_row_t<std::tuple_element_t<2, HubAtoms>>, MemoryMappingRow>;
    }
}

static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::sense_hub_syscall_atoms>());
static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::pmu_sample_syscall_atoms>());
static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::lock_contention_syscall_atoms>());
static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::sched_switch_syscall_atoms>());
static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::sched_tp_btf_syscall_atoms>());
static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::syscall_tp_btf_syscall_atoms>());
static_assert(names_bpf_perf_event_open_and_mmap_<::crucible::perf::syscall_latency_syscall_atoms>());

// The check above refuses a list that differs.
static_assert(!names_bpf_perf_event_open_and_mmap_<std::tuple<sc::per<sc::SyscallId::bpf>>>());
static_assert(!names_bpf_perf_event_open_and_mmap_<
              std::tuple<sc::per<sc::SyscallId::bpf>, sc::per<sc::SyscallId::mmap>,
                         sc::per<sc::SyscallId::perf_event_open>>>());

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
