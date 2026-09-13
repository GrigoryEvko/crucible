// The hardening mint carries a type-level list of the privileged syscalls
// its apply path issues.  The list is an audit trail, and it is only worth
// anything while it still matches the syscalls actually made.

#include <crucible/warden/Hardening.h>
#include <crucible/fixy/syscall/Bridge.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace {

namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
namespace eff = ::crucible::effects;
namespace fg = ::crucible::fixy::grant;

// The ordinals are append-only: an existing one keeps its value forever,
// so a federation cache key that hashed a syscall id never drifts.  Adding
// an enumerator anywhere but the end invalidates every such key.
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::sched_setattr) == 36);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::mlock2) == 37);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::mlock) == 38);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::munlock) == 39);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::prctl) == 40);

static_assert(fsc::family_of(fsc::SyscallId::sched_setattr) == fll::SyscallFamily::ThreadSync);
static_assert(fsc::family_of(fsc::SyscallId::mlock2) == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::mlock) == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::munlock) == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::prctl) == fll::SyscallFamily::Privilege);

// An established classification must not shift when the catalog grows.
static_assert(fsc::family_of(fsc::SyscallId::madvise) == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::sched_setaffinity) == fll::SyscallFamily::ThreadSync);

using HG = ::crucible::warden::mint_hardening_syscall_grants;

static_assert(std::tuple_size_v<HG> == 7, "the hardening grants must enumerate exactly the 7 syscalls the apply "
                                          "path issues: sched_setaffinity, sched_setattr, mlock, mlock2, "
                                          "munlock, madvise and prctl.  Drift between the declared set and the "
                                          "syscalls actually issued is an admission soundness regression.");

static_assert(std::is_same_v<std::tuple_element_t<0, HG>, fsc::per<fsc::SyscallId::sched_setaffinity>>);
static_assert(std::is_same_v<std::tuple_element_t<1, HG>, fsc::per<fsc::SyscallId::sched_setattr>>);
static_assert(std::is_same_v<std::tuple_element_t<2, HG>, fsc::per<fsc::SyscallId::mlock>>);
static_assert(std::is_same_v<std::tuple_element_t<3, HG>, fsc::per<fsc::SyscallId::mlock2>>);
static_assert(std::is_same_v<std::tuple_element_t<4, HG>, fsc::per<fsc::SyscallId::munlock>>);
static_assert(std::is_same_v<std::tuple_element_t<5, HG>, fsc::per<fsc::SyscallId::madvise>>);
static_assert(std::is_same_v<std::tuple_element_t<6, HG>, fsc::per<fsc::SyscallId::prctl>>);

using ThreadSyncRow = ::crucible::effects::Row<::crucible::effects::Effect::Block>;
using MemoryMappingRow = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
using PrivilegeRow = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;

namespace fxbr = ::crucible::fixy::syscall::bridge;

static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::sched_setattr>>, ThreadSyncRow>);
static_assert(
    std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::sched_setaffinity>>, ThreadSyncRow>);
static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::mlock>>, MemoryMappingRow>);
static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::mlock2>>, MemoryMappingRow>);
static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::munlock>>, MemoryMappingRow>);
static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::madvise>>, MemoryMappingRow>);
static_assert(std::is_same_v<fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::prctl>>, PrivilegeRow>);

// The gate is the Init row.  The syscall-grant list is a classification
// that sits beside that gate.  It does not tighten the row.
static_assert(::crucible::warden::CtxFitsHardeningMint<eff::ColdInitCtx>);
static_assert(!::crucible::warden::CtxFitsHardeningMint<eff::HotFgCtx>);
static_assert(!::crucible::warden::CtxFitsHardeningMint<eff::BgDrainCtx>);

// Two syscalls in the same family still need separate types, because each
// gets its own federation cache slot.
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::sched_setattr>, fsc::per<fsc::SyscallId::sched_setaffinity>>);
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::mlock>, fsc::per<fsc::SyscallId::mlock2>>);
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::mlock>, fsc::per<fsc::SyscallId::munlock>>);
static_assert(!std::is_same_v<fsc::per<fsc::SyscallId::prctl>, fsc::per<fsc::SyscallId::ptrace>>);

}  // namespace

int main() {
    // Every claim here is a compile-time one.  Applying the hardening for
    // real needs capabilities an unprivileged test run does not have.
    return 0;
}
