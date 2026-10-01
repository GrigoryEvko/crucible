// The compile-time checks of crucible/warden/Hardening.h.

#include <crucible/warden/Hardening.h>

namespace crucible::warden {

// The family of each atom is checked against the table here, so a
// disagreement fails the build of this check file rather than a later
// call site.
namespace detail::hardening_syscall_check {
namespace sc = ::fixy::atom::syscall;

static_assert(sc::per<sc::SyscallId::sched_setaffinity>::family == sc::SyscallFamily::ThreadSync);
static_assert(sc::per<sc::SyscallId::sched_setattr>::family == sc::SyscallFamily::ThreadSync);
static_assert(sc::per<sc::SyscallId::mlock>::family == sc::SyscallFamily::MemoryMapping);
static_assert(sc::per<sc::SyscallId::mlock2>::family == sc::SyscallFamily::MemoryMapping);
static_assert(sc::per<sc::SyscallId::munlock>::family == sc::SyscallFamily::MemoryMapping);
static_assert(sc::per<sc::SyscallId::madvise>::family == sc::SyscallFamily::MemoryMapping);
static_assert(sc::per<sc::SyscallId::prctl>::family == sc::SyscallFamily::Privilege);
static_assert(sc::per<sc::SyscallId::sched_getaffinity>::family == sc::SyscallFamily::ThreadSync);
static_assert(sc::per<sc::SyscallId::sched_getattr>::family == sc::SyscallFamily::ThreadSync);

static_assert(std::tuple_size_v<hardening_syscall_atoms> == 9,
              "hardening_syscall_atoms no longer holds 9 entries.  A syscall added to Hardening::apply() needs "
              "an entry in the tuple and a family check beside it.  A syscall removed from the set changes the "
              "cache key derived from it, so audit the removal first.  This count agrees with the code only "
              "because utils/scripts/check-syscall-grant-coverage.py derives the set from the call sites.  A "
              "hand-written count is a claim about the code that nothing reads the code to confirm.");
}  // namespace detail::hardening_syscall_check

static_assert(CtxFitsHardeningMint<::fixy::InitLoadCtx>);
static_assert(!CtxFitsHardeningMint<::fixy::ColdInitCtx>,
              "The cold init context owns no Block, so it cannot read sysfs.");
static_assert(!CtxFitsHardeningMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsHardeningMint<::fixy::HotFgCtx>);

}  // namespace crucible::warden
