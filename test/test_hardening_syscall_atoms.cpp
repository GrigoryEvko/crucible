// The hardening mint carries a type-level list of the privileged syscalls
// its apply path issues.  The list is an audit trail, and it is only worth
// anything while it still matches the syscalls actually made.

#include <crucible/warden/Hardening.h>
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
// so a federation cache key that hashed a syscall id never drifts.  Adding
// an enumerator anywhere but the end invalidates every such key.
static_assert(std::to_underlying(sc::SyscallId::sched_setattr) == 36);
static_assert(std::to_underlying(sc::SyscallId::mlock2) == 37);
static_assert(std::to_underlying(sc::SyscallId::mlock) == 38);
static_assert(std::to_underlying(sc::SyscallId::munlock) == 39);
static_assert(std::to_underlying(sc::SyscallId::prctl) == 40);
static_assert(std::to_underlying(sc::SyscallId::sched_getaffinity) == 43);
static_assert(std::to_underlying(sc::SyscallId::sched_getattr) == 44);

using HardeningAtoms = ::crucible::warden::hardening_syscall_atoms;

// This count is a claim about the code that no compile-time check can
// confirm, because the code it describes is a set of call sites rather
// than a type.  scripts/check-syscall-grant-coverage.sh reads those call
// sites and compares them with the tuple.  That script, not this line, is
// what keeps the two honest.
static_assert(std::tuple_size_v<HardeningAtoms> == 9,
              "the hardening atoms must enumerate exactly the 9 syscalls the apply path issues: "
              "sched_setaffinity, sched_setattr, mlock, mlock2, munlock, madvise, prctl, sched_getaffinity and "
              "sched_getattr.");

static_assert(std::is_same_v<std::tuple_element_t<0, HardeningAtoms>, sc::per<sc::SyscallId::sched_setaffinity>>);
static_assert(std::is_same_v<std::tuple_element_t<1, HardeningAtoms>, sc::per<sc::SyscallId::sched_setattr>>);
static_assert(std::is_same_v<std::tuple_element_t<2, HardeningAtoms>, sc::per<sc::SyscallId::mlock>>);
static_assert(std::is_same_v<std::tuple_element_t<3, HardeningAtoms>, sc::per<sc::SyscallId::mlock2>>);
static_assert(std::is_same_v<std::tuple_element_t<4, HardeningAtoms>, sc::per<sc::SyscallId::munlock>>);
static_assert(std::is_same_v<std::tuple_element_t<5, HardeningAtoms>, sc::per<sc::SyscallId::madvise>>);
static_assert(std::is_same_v<std::tuple_element_t<6, HardeningAtoms>, sc::per<sc::SyscallId::prctl>>);
static_assert(std::is_same_v<std::tuple_element_t<7, HardeningAtoms>, sc::per<sc::SyscallId::sched_getaffinity>>);
static_assert(std::is_same_v<std::tuple_element_t<8, HardeningAtoms>, sc::per<sc::SyscallId::sched_getattr>>);

// The row each atom lifts to.  A mapping call lifts to IO and Block;
// fixy/atoms/Syscall.h says why.
using ThreadSyncRow = fe::Row<fe::Effect::Block>;
using MemoryMappingRow = fe::Row<fe::Effect::IO, fe::Effect::Block>;
using PrivilegeRow = fe::Row<fe::Effect::IO, fe::Effect::Block>;

static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::sched_setaffinity>>, ThreadSyncRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::sched_setattr>>, ThreadSyncRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::mlock>>, MemoryMappingRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::mlock2>>, MemoryMappingRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::munlock>>, MemoryMappingRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::madvise>>, MemoryMappingRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::prctl>>, PrivilegeRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::sched_getaffinity>>, ThreadSyncRow>);
static_assert(std::is_same_v<fe::lift_row_t<sc::per<sc::SyscallId::sched_getattr>>, ThreadSyncRow>);

// The gate is the Init row.  The syscall list is a classification that
// sits beside that gate.  It does not tighten the row.
static_assert(::crucible::warden::CtxFitsHardeningMint<eff::ColdInitCtx>);
static_assert(!::crucible::warden::CtxFitsHardeningMint<eff::HotFgCtx>);
static_assert(!::crucible::warden::CtxFitsHardeningMint<eff::BgDrainCtx>);

// Two syscalls in the same family still need separate types, because each
// gets its own federation cache slot.
static_assert(!std::is_same_v<sc::per<sc::SyscallId::sched_setattr>, sc::per<sc::SyscallId::sched_setaffinity>>);
static_assert(!std::is_same_v<sc::per<sc::SyscallId::mlock>, sc::per<sc::SyscallId::mlock2>>);
static_assert(!std::is_same_v<sc::per<sc::SyscallId::mlock>, sc::per<sc::SyscallId::munlock>>);
static_assert(!std::is_same_v<sc::per<sc::SyscallId::prctl>, sc::per<sc::SyscallId::ptrace>>);

}  // namespace

int main() {
    // Every claim here is a compile-time one.  Applying the hardening for
    // real needs capabilities an unprivileged test run does not have.
    return 0;
}
