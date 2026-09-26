// Each perf hub load issues bpf(), perf_event_open() and mmap().  The row
// that the context gate of each mint demands must contain the effect row
// that each of these three calls lifts to.  A gate that stops short of one
// of them would let a context reach a mint that makes a call the context
// does not claim.

#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <fixy/Ctx.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <type_traits>
#include <utility>

namespace {

namespace sc = ::fixy::atom::syscall;
namespace fe = ::foundation::effects;
namespace perf = ::crucible::perf;

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

static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::bpf>>, PrivilegeRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::perf_event_open>>, PrivilegeRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::mmap>>, MemoryMappingRow>);

// The row a gate demands holds the lifted row of every call the load
// makes.
template <class RequiredRow>
[[nodiscard]] consteval bool covers_the_load_calls_() noexcept {
    return fe::Subrow<fe::lift_row_t<sc::per<sc::SyscallId::bpf>>, RequiredRow>
        && fe::Subrow<fe::lift_row_t<sc::per<sc::SyscallId::perf_event_open>>, RequiredRow>
        && fe::Subrow<fe::lift_row_t<sc::per<sc::SyscallId::mmap>>, RequiredRow>;
}

static_assert(covers_the_load_calls_<perf::sense_hub_required_row>());
static_assert(covers_the_load_calls_<perf::pmu_sample_required_row>());
static_assert(covers_the_load_calls_<perf::lock_contention_required_row>());
static_assert(covers_the_load_calls_<perf::sched_switch_required_row>());
static_assert(covers_the_load_calls_<perf::sched_tp_btf_required_row>());
static_assert(covers_the_load_calls_<perf::syscall_tp_btf_required_row>());
static_assert(covers_the_load_calls_<perf::syscall_latency_required_row>());

// The check above refuses a row that stops short of one call.
static_assert(!covers_the_load_calls_<fe::Row<fe::Effect::Alloc, fe::Effect::IO>>());
static_assert(!covers_the_load_calls_<fe::Row<>>());

// Each load calls bpf(BPF_PROG_LOAD) and waits on the kernel verifier, so
// the gate refuses every context whose row has no Block.  The startup
// load context, the background load context and the test context claim
// Block, and each gate admits all three.
template <template <class> class Gate>
[[nodiscard]] consteval bool admits_only_the_blocking_contexts_() noexcept {
    return !Gate<::fixy::ColdInitCtx>::value && !Gate<::fixy::BgCompileCtx>::value
        && !Gate<::fixy::BgDrainCtx>::value && !Gate<::fixy::HotFgCtx>::value
        && Gate<::fixy::InitLoadCtx>::value && Gate<::fixy::BgLoadCtx>::value
        && Gate<::fixy::TestRunnerCtx>::value;
}

template <class Ctx>
using sense_hub_gate = std::bool_constant<perf::CtxFitsSenseHubMint<Ctx>>;
template <class Ctx>
using pmu_sample_gate = std::bool_constant<perf::CtxFitsPmuSampleMint<Ctx>>;
template <class Ctx>
using lock_contention_gate = std::bool_constant<perf::CtxFitsLockContentionMint<Ctx>>;
template <class Ctx>
using sched_switch_gate = std::bool_constant<perf::CtxFitsSchedSwitchMint<Ctx>>;
template <class Ctx>
using sched_tp_btf_gate = std::bool_constant<perf::CtxFitsSchedTpBtfMint<Ctx>>;
template <class Ctx>
using syscall_tp_btf_gate = std::bool_constant<perf::CtxFitsSyscallTpBtfMint<Ctx>>;
template <class Ctx>
using syscall_latency_gate = std::bool_constant<perf::CtxFitsSyscallLatencyMint<Ctx>>;

static_assert(admits_only_the_blocking_contexts_<sense_hub_gate>());
static_assert(admits_only_the_blocking_contexts_<pmu_sample_gate>());
static_assert(admits_only_the_blocking_contexts_<lock_contention_gate>());
static_assert(admits_only_the_blocking_contexts_<sched_switch_gate>());
static_assert(admits_only_the_blocking_contexts_<sched_tp_btf_gate>());
static_assert(admits_only_the_blocking_contexts_<syscall_tp_btf_gate>());
static_assert(admits_only_the_blocking_contexts_<syscall_latency_gate>());

}  // namespace

int main() {
    // Every claim here is a compile-time one.  Calling these mints for
    // real needs capabilities an unprivileged test run does not have.
    return 0;
}
