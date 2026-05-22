// FIXY-V-098 sentinel TU: fixy/syscall/Family.h + fixy/syscall/Per.h.
//
// V-098 ships the V-097-axis grant catalog:
//   1. fixy/syscall/Family.h — 9 family-tier grants (one per
//      SyscallFamily enumerator); each routes which_dim<> →
//      SyscallSurface and pins family_tier_v<> at the matching tier.
//   2. fixy/syscall/Per.h — 36-enumerator SyscallId catalog + parametric
//      per<Id> grant; each per<> instantiation routes which_dim<> →
//      SyscallSurface and derives family_tier_v<> via family_of(Id).
//
// V-099 ships per-vendor + per-kernel-subsystem ioctl grants (deferred);
// V-100 ships the bridge that lifts a SyscallSurface pin into the
// Met(X) effect row automatically (deferred).  This sentinel TU
// witnesses that the V-098 surface is structurally consistent end-to-
// end — pairwise distinctness, exhaustive family_of() coverage,
// cross-tier engagement-machinery sanity vs Family.h.
//
// Why a sentinel TU vs header-only static_asserts: per
// feedback_header_only_static_assert_blind_spot — headers shipped with
// embedded static_asserts aren't verified under project warning flags
// unless a .cpp TU includes them.  This TU forces both headers through
// the project's default compile preset.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>

#include <type_traits>
#include <utility>

namespace cg  = ::crucible::fixy::grant;
namespace cgs = ::crucible::fixy::grant::syscall;
namespace cal = ::crucible::algebra::lattices;
namespace cs  = ::crucible::safety;
namespace cfd = ::crucible::fixy::dim;

namespace {

// ── Layer 1: family_tier_v cross-check against family_of() ──────────
// Per.h's family_tier specialization derives via family_of(); confirm
// the round-trip is consistent for a sample at every tier boundary.
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::clock_gettime>>
              == cgs::family_of(cgs::SyscallId::clock_gettime));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::write>>
              == cgs::family_of(cgs::SyscallId::write));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::mmap>>
              == cgs::family_of(cgs::SyscallId::mmap));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::futex>>
              == cgs::family_of(cgs::SyscallId::futex));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::sendmsg>>
              == cgs::family_of(cgs::SyscallId::sendmsg));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::clone>>
              == cgs::family_of(cgs::SyscallId::clone));
static_assert(cg::family_tier_v<cgs::per<cgs::SyscallId::ptrace>>
              == cgs::family_of(cgs::SyscallId::ptrace));

// ── Layer 2: Family.h + Per.h share the family_tier metadata channel ─
// A binding declaring family_thread_sync and a binding declaring
// per<SyscallId::futex> agree on their family_tier_v<>; downstream
// consumers (V-100 bridge) see the same SyscallFamily either way.
static_assert(cg::family_tier_v<cgs::family_thread_sync>
              == cg::family_tier_v<cgs::per<cgs::SyscallId::futex>>);
static_assert(cg::family_tier_v<cgs::family_file_mutation>
              == cg::family_tier_v<cgs::per<cgs::SyscallId::write>>);
static_assert(cg::family_tier_v<cgs::family_memory_mapping>
              == cg::family_tier_v<cgs::per<cgs::SyscallId::mmap>>);
static_assert(cg::family_tier_v<cgs::family_network_io>
              == cg::family_tier_v<cgs::per<cgs::SyscallId::socket>>);
static_assert(cg::family_tier_v<cgs::family_process_control>
              == cg::family_tier_v<cgs::per<cgs::SyscallId::clone>>);
static_assert(cg::family_tier_v<cgs::family_privilege>
              == cg::family_tier_v<cgs::per<cgs::SyscallId::ptrace>>);

// ── Layer 3: which_dim routing — both surfaces → SyscallSurface ─────
// Family.h's tags AND Per.h's per<> instantiations route to the SAME
// DimensionAxis; this is what triggers the V-098 duplicate-engagement
// rejection when a binding attempts BOTH a family grant AND a per<>
// grant on overlapping tiers.
static_assert(cg::which_dim_v<cgs::family_no_syscall>
              == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgs::per<cgs::SyscallId::clock_gettime>>
              == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgs::family_privilege>
              == cg::which_dim_v<cgs::per<cgs::SyscallId::capset>>);

// ── Layer 4: pairwise type-distinctness across Family.h × Per.h ─────
// A family_* tag and a per<Id> tag with the same family classification
// are STRUCTURALLY distinct types, so the duplicate-engagement gate
// rejects (rather than silently collapses) the combination.
static_assert(!std::is_same_v<cgs::family_vdso_only,
                              cgs::per<cgs::SyscallId::clock_gettime>>);
static_assert(!std::is_same_v<cgs::family_file_mutation,
                              cgs::per<cgs::SyscallId::write>>);
static_assert(!std::is_same_v<cgs::family_memory_mapping,
                              cgs::per<cgs::SyscallId::mmap>>);
static_assert(!std::is_same_v<cgs::family_thread_sync,
                              cgs::per<cgs::SyscallId::futex>>);
static_assert(!std::is_same_v<cgs::family_network_io,
                              cgs::per<cgs::SyscallId::socket>>);

// ── Layer 5: family_of() exhaustive on the SyscallId catalog ────────
// Touch every SyscallId enumerator so the constexpr classifier's switch
// is forced through (catches a missing arm — which silently falls to
// the Privilege fallback at the per<> family_tier specialization).
constexpr auto exhaustive_family_of_witness() {
    using SF = cal::SyscallFamily;
    auto all_classified =
        cgs::family_of(cgs::SyscallId::clock_gettime)     == SF::VdsoOnly
     && cgs::family_of(cgs::SyscallId::clock_getres)      == SF::VdsoOnly
     && cgs::family_of(cgs::SyscallId::getcpu_vdso)       == SF::VdsoOnly
     && cgs::family_of(cgs::SyscallId::gettimeofday)      == SF::VdsoOnly
     && cgs::family_of(cgs::SyscallId::getpid)            == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::getppid)           == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::getuid)            == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::geteuid)           == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::getgid)            == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::gettid)            == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::uname)             == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::sysinfo)           == SF::ReadOnlyState
     && cgs::family_of(cgs::SyscallId::open)              == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::openat)            == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::close)             == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::read)              == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::write)             == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::pread)             == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::pwrite)            == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::fsync)             == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::fdatasync)         == SF::FileMutation
     && cgs::family_of(cgs::SyscallId::mmap)              == SF::MemoryMapping
     && cgs::family_of(cgs::SyscallId::munmap)            == SF::MemoryMapping
     && cgs::family_of(cgs::SyscallId::mprotect)          == SF::MemoryMapping
     && cgs::family_of(cgs::SyscallId::madvise)           == SF::MemoryMapping
     && cgs::family_of(cgs::SyscallId::futex)             == SF::ThreadSync
     && cgs::family_of(cgs::SyscallId::sched_yield)       == SF::ThreadSync
     && cgs::family_of(cgs::SyscallId::sched_setaffinity) == SF::ThreadSync
     && cgs::family_of(cgs::SyscallId::socket)            == SF::NetworkIo
     && cgs::family_of(cgs::SyscallId::connect)           == SF::NetworkIo
     && cgs::family_of(cgs::SyscallId::sendmsg)           == SF::NetworkIo
     && cgs::family_of(cgs::SyscallId::recvmsg)           == SF::NetworkIo
     && cgs::family_of(cgs::SyscallId::clone)             == SF::ProcessControl
     && cgs::family_of(cgs::SyscallId::execve)            == SF::ProcessControl
     && cgs::family_of(cgs::SyscallId::ptrace)            == SF::Privilege
     && cgs::family_of(cgs::SyscallId::capset)            == SF::Privilege;
    return all_classified;
}
static_assert(exhaustive_family_of_witness(),
    "FIXY-V-098: family_of(SyscallId) classifier disagrees with the "
    "tier assignment for at least one of the 36 shipped enumerators.");

// ── Layer 6: SyscallId underlying type pinned uint16_t ──────────────
// The catalog reserves ordinals up to 65535 — V-099 ioctl additions and
// V-100 bridge expansions can grow within this range without breaking
// the underlying ABI.
static_assert(std::is_same_v<std::underlying_type_t<cgs::SyscallId>,
                             std::uint16_t>);

}  // namespace

int main() { return 0; }
