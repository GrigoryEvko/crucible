#pragma once

// The syscall surface is a meta-axis. It composes family tiers, per-syscall
// identities and per-vendor ioctl operations, so its grant tags live apart
// from the tags grouped by safety-lattice taxonomy.
//
// A binding declares exactly one syscall-axis grant: a family tier from this
// header or a per-syscall identity. Declaring both is a duplicate engagement
// on the one axis and is rejected.

#include <crucible/fixy/Grant.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>

#include <type_traits>

namespace crucible::fixy::grant {

// The primary has no definition. A tag with no mapping is a hard error at
// the point of use rather than a silent default tier.
template <typename G>
struct family_tier;

template <typename G>
inline constexpr ::crucible::algebra::lattices::SyscallFamily family_tier_v = family_tier<G>::value;

namespace syscall {

struct family_no_syscall final : grant_base {};

// Only vDSO-resolved calls, such as clock_gettime and getcpu.
struct family_vdso_only final : grant_base {};

// Read-only metadata queries, such as getpid, uname and sysinfo.
struct family_read_only_state final : grant_base {};

// File-handle work, such as open, read, write and close.
struct family_file_mutation final : grant_base {};

// The mmap family, such as mmap, mprotect and madvise.
struct family_memory_mapping final : grant_base {};

// Synchronization primitives, such as futex and sched_setaffinity.
struct family_thread_sync final : grant_base {};

// The socket family, such as socket, bind and sendmsg.
struct family_network_io final : grant_base {};

// Process lifecycle, such as clone, execve and wait4.
struct family_process_control final : grant_base {};

// Privileged operations, such as capset, mount, ptrace and setuid.
struct family_privilege final : grant_base {};

}  // namespace syscall

template <>
struct which_dim<syscall::family_no_syscall>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_vdso_only>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_read_only_state>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_file_mutation>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_memory_mapping>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_thread_sync>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_network_io>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_process_control>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_privilege>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <>
struct family_tier<syscall::family_no_syscall>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::NoSyscall> {};
template <>
struct family_tier<syscall::family_vdso_only>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::VdsoOnly> {};
template <>
struct family_tier<syscall::family_read_only_state>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::ReadOnlyState> {};
template <>
struct family_tier<syscall::family_file_mutation>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::FileMutation> {};
template <>
struct family_tier<syscall::family_memory_mapping>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::MemoryMapping> {};
template <>
struct family_tier<syscall::family_thread_sync>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::ThreadSync> {};
template <>
struct family_tier<syscall::family_network_io>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::NetworkIo> {};
template <>
struct family_tier<syscall::family_process_control>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::ProcessControl> {};
template <>
struct family_tier<syscall::family_privilege>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::Privilege> {};

namespace detail::syscall_family_grant_self_test {

namespace sc = syscall;
namespace al = ::crucible::algebra::lattices;
using D = dim::DimensionAxis;
using SF = al::SyscallFamily;

static_assert(IsGrantTag<sc::family_no_syscall>);
static_assert(IsGrantTag<sc::family_vdso_only>);
static_assert(IsGrantTag<sc::family_read_only_state>);
static_assert(IsGrantTag<sc::family_file_mutation>);
static_assert(IsGrantTag<sc::family_memory_mapping>);
static_assert(IsGrantTag<sc::family_thread_sync>);
static_assert(IsGrantTag<sc::family_network_io>);
static_assert(IsGrantTag<sc::family_process_control>);
static_assert(IsGrantTag<sc::family_privilege>);

static_assert(sizeof(sc::family_no_syscall) == 1);
static_assert(sizeof(sc::family_vdso_only) == 1);
static_assert(sizeof(sc::family_read_only_state) == 1);
static_assert(sizeof(sc::family_file_mutation) == 1);
static_assert(sizeof(sc::family_memory_mapping) == 1);
static_assert(sizeof(sc::family_thread_sync) == 1);
static_assert(sizeof(sc::family_network_io) == 1);
static_assert(sizeof(sc::family_process_control) == 1);
static_assert(sizeof(sc::family_privilege) == 1);

static_assert(which_dim_v<sc::family_no_syscall> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_vdso_only> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_read_only_state> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_file_mutation> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_memory_mapping> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_thread_sync> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_network_io> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_process_control> == D::SyscallSurface);
static_assert(which_dim_v<sc::family_privilege> == D::SyscallSurface);

static_assert(family_tier_v<sc::family_no_syscall> == SF::NoSyscall);
static_assert(family_tier_v<sc::family_vdso_only> == SF::VdsoOnly);
static_assert(family_tier_v<sc::family_read_only_state> == SF::ReadOnlyState);
static_assert(family_tier_v<sc::family_file_mutation> == SF::FileMutation);
static_assert(family_tier_v<sc::family_memory_mapping> == SF::MemoryMapping);
static_assert(family_tier_v<sc::family_thread_sync> == SF::ThreadSync);
static_assert(family_tier_v<sc::family_network_io> == SF::NetworkIo);
static_assert(family_tier_v<sc::family_process_control> == SF::ProcessControl);
static_assert(family_tier_v<sc::family_privilege> == SF::Privilege);

// Adjacent tiers plus one cross-chain sample. Distinct types keep two tags
// in one pack from collapsing into a single engagement.
static_assert(!std::is_same_v<sc::family_no_syscall, sc::family_vdso_only>);
static_assert(!std::is_same_v<sc::family_vdso_only, sc::family_read_only_state>);
static_assert(!std::is_same_v<sc::family_read_only_state, sc::family_file_mutation>);
static_assert(!std::is_same_v<sc::family_file_mutation, sc::family_memory_mapping>);
static_assert(!std::is_same_v<sc::family_memory_mapping, sc::family_thread_sync>);
static_assert(!std::is_same_v<sc::family_thread_sync, sc::family_network_io>);
static_assert(!std::is_same_v<sc::family_network_io, sc::family_process_control>);
static_assert(!std::is_same_v<sc::family_process_control, sc::family_privilege>);
static_assert(!std::is_same_v<sc::family_no_syscall, sc::family_privilege>);

// The predicate witnesses that every tier is reached by at least one tag.
// Together with the tier-count assertion below that makes the map a
// bijection.
[[nodiscard]] consteval bool every_family_tier_covered() noexcept {
    using sf2 = al::SyscallFamily;
    return family_tier_v<sc::family_no_syscall> == sf2::NoSyscall
        && family_tier_v<sc::family_vdso_only> == sf2::VdsoOnly
        && family_tier_v<sc::family_read_only_state> == sf2::ReadOnlyState
        && family_tier_v<sc::family_file_mutation> == sf2::FileMutation
        && family_tier_v<sc::family_memory_mapping> == sf2::MemoryMapping
        && family_tier_v<sc::family_thread_sync> == sf2::ThreadSync
        && family_tier_v<sc::family_network_io> == sf2::NetworkIo
        && family_tier_v<sc::family_process_control> == sf2::ProcessControl
        && family_tier_v<sc::family_privilege> == sf2::Privilege;
}
static_assert(every_family_tier_covered(), "the nine family tags must collectively cover the nine "
                                           "SyscallFamily tiers exactly once.  If this fires, the family_tier "
                                           "specialization for at least one tag is wrong or missing.");

static_assert(::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
              "the family_tier map depends on a 9-tier chain.  If the lattice grew "
              "an enumerator, every family_tier specialization above is stale.");

}  // namespace detail::syscall_family_grant_self_test

}  // namespace crucible::fixy::grant
