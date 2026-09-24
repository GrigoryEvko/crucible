// A header's embedded static_asserts are only checked under the project
// warning flags when a translation unit in the build graph includes it.
// The two includes below are what force both grant catalogs through the
// default compile preset.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>

#include <type_traits>
#include <utility>

namespace cg = ::crucible::fixy::grant;
namespace cgs = ::crucible::fixy::grant::syscall;
namespace cal = ::crucible::algebra::lattices;
namespace cs = ::crucible::safety;
namespace cfd = ::crucible::fixy::dim;

namespace {

// A per-call grant derives its tier through the classifier, so the round
// trip must agree.  The sample takes one call from each tier.
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::clock_gettime>>
              == cgs::family_of(cgs::SyscallId::clock_gettime));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::write>> == cgs::family_of(cgs::SyscallId::write));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::mmap>> == cgs::family_of(cgs::SyscallId::mmap));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::futex>> == cgs::family_of(cgs::SyscallId::futex));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::sendmsg>> == cgs::family_of(cgs::SyscallId::sendmsg));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::clone>> == cgs::family_of(cgs::SyscallId::clone));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::ptrace>> == cgs::family_of(cgs::SyscallId::ptrace));

// The two authoring surfaces share one metadata channel, so a whole
// family grant and a per-call grant inside it report the same tier and a
// downstream consumer sees no difference between them.
static_assert(cg::family_tier_v<cgs::family_thread_sync> == cg::family_tier_v<cgs::per<cgs::SyscallId::futex>>);
static_assert(cg::family_tier_v<cgs::family_file_mutation> == cg::family_tier_v<cgs::per<cgs::SyscallId::write>>);
static_assert(cg::family_tier_v<cgs::family_memory_mapping> == cg::family_tier_v<cgs::per<cgs::SyscallId::mmap>>);
static_assert(cg::family_tier_v<cgs::family_network_io> == cg::family_tier_v<cgs::per<cgs::SyscallId::socket>>);
static_assert(cg::family_tier_v<cgs::family_process_control> == cg::family_tier_v<cgs::per<cgs::SyscallId::clone>>);
static_assert(cg::family_tier_v<cgs::family_privilege> == cg::family_tier_v<cgs::per<cgs::SyscallId::ptrace>>);

// Both surfaces route to one axis.  That is what makes a binding
// declaring a family grant and a per-call grant at once fire the
// duplicate-engagement rejection instead of quietly accepting both.
static_assert(cg::which_dim_v<cgs::family_no_syscall> == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgs::per<cgs::SyscallId::clock_gettime>> == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgs::family_privilege> == cg::which_dim_v<cgs::per<cgs::SyscallId::capset>>);

// Two grants that classify into one family are still distinct types, so
// the gate above has something to reject rather than collapsing them.
static_assert(!std::is_same_v<cgs::family_vdso_only, cgs::per<cgs::SyscallId::clock_gettime>>);
static_assert(!std::is_same_v<cgs::family_file_mutation, cgs::per<cgs::SyscallId::write>>);
static_assert(!std::is_same_v<cgs::family_memory_mapping, cgs::per<cgs::SyscallId::mmap>>);
static_assert(!std::is_same_v<cgs::family_thread_sync, cgs::per<cgs::SyscallId::futex>>);
static_assert(!std::is_same_v<cgs::family_network_io, cgs::per<cgs::SyscallId::socket>>);

// Every enumerator is named here so that a missing switch arm shows up.
// Without this, an unclassified call falls through to the privilege
// fallback and looks like a deliberate classification.
constexpr auto exhaustive_family_of_witness() {
    using SF = cal::SyscallFamily;
    auto all_classified = cgs::family_of(cgs::SyscallId::clock_gettime) == SF::VdsoOnly
                       && cgs::family_of(cgs::SyscallId::clock_getres) == SF::VdsoOnly
                       && cgs::family_of(cgs::SyscallId::getcpu_vdso) == SF::VdsoOnly
                       && cgs::family_of(cgs::SyscallId::gettimeofday) == SF::VdsoOnly
                       && cgs::family_of(cgs::SyscallId::getpid) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::getppid) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::getuid) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::geteuid) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::getgid) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::gettid) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::uname) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::sysinfo) == SF::ReadOnlyState
                       && cgs::family_of(cgs::SyscallId::open) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::openat) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::close) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::read) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::write) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::pread) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::pwrite) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::fsync) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::fdatasync) == SF::FileMutation
                       && cgs::family_of(cgs::SyscallId::mmap) == SF::MemoryMapping
                       && cgs::family_of(cgs::SyscallId::munmap) == SF::MemoryMapping
                       && cgs::family_of(cgs::SyscallId::mprotect) == SF::MemoryMapping
                       && cgs::family_of(cgs::SyscallId::madvise) == SF::MemoryMapping
                       && cgs::family_of(cgs::SyscallId::futex) == SF::ThreadSync
                       && cgs::family_of(cgs::SyscallId::sched_yield) == SF::ThreadSync
                       && cgs::family_of(cgs::SyscallId::sched_setaffinity) == SF::ThreadSync
                       && cgs::family_of(cgs::SyscallId::socket) == SF::NetworkIo
                       && cgs::family_of(cgs::SyscallId::connect) == SF::NetworkIo
                       && cgs::family_of(cgs::SyscallId::sendmsg) == SF::NetworkIo
                       && cgs::family_of(cgs::SyscallId::recvmsg) == SF::NetworkIo
                       && cgs::family_of(cgs::SyscallId::clone) == SF::ProcessControl
                       && cgs::family_of(cgs::SyscallId::execve) == SF::ProcessControl
                       && cgs::family_of(cgs::SyscallId::ptrace) == SF::Privilege
                       && cgs::family_of(cgs::SyscallId::capset) == SF::Privilege;
    return all_classified;
}
static_assert(exhaustive_family_of_witness(), "The family_of(SyscallId) classifier disagrees with the tier "
                                              "assignment for at least one enumerator.");

// A 16-bit underlying type reserves ordinal space the catalog can grow
// into without disturbing the values already assigned.
static_assert(std::is_same_v<std::underlying_type_t<cgs::SyscallId>, std::uint16_t>);

}  // namespace

int main() { return 0; }
