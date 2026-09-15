#pragma once

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/Grant.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::grant::syscall {

// The ordinals are frozen.  A new syscall appends at the next free ordinal.
// Reusing or reordering one silently changes every stored row hash, which is
// a federation cache key, and nothing diagnoses it.
enum class SyscallId : std::uint16_t {
    clock_gettime = 0,
    clock_getres = 1,
    getcpu_vdso = 2,
    gettimeofday = 3,

    getpid = 4,
    getppid = 5,
    getuid = 6,
    geteuid = 7,
    getgid = 8,
    gettid = 9,
    uname = 10,
    sysinfo = 11,

    open = 12,
    openat = 13,
    close = 14,
    read = 15,
    write = 16,
    pread = 17,
    pwrite = 18,
    fsync = 19,
    fdatasync = 20,

    mmap = 21,
    munmap = 22,
    mprotect = 23,
    madvise = 24,

    futex = 25,
    sched_yield = 26,
    sched_setaffinity = 27,

    socket = 28,
    connect = 29,
    sendmsg = 30,
    recvmsg = 31,

    clone = 32,
    execve = 33,

    ptrace = 34,
    capset = 35,

    sched_setattr = 36,
    mlock2 = 37,
    mlock = 38,
    munlock = 39,
    prctl = 40,

    bpf = 41,
    perf_event_open = 42,

    // The read halves of the two scheduler knobs above. Hardening::apply
    // reads the prior affinity mask and the prior scheduling attributes
    // before it writes new ones, so the audit set has to name them.
    sched_getaffinity = 43,
    sched_getattr = 44,
};

[[nodiscard]] constexpr ::crucible::algebra::lattices::SyscallFamily family_of(SyscallId id) noexcept {
    using SF = ::crucible::algebra::lattices::SyscallFamily;
    switch (id) {
        case SyscallId::clock_gettime:
            return SF::VdsoOnly;
        case SyscallId::clock_getres:
            return SF::VdsoOnly;
        case SyscallId::getcpu_vdso:
            return SF::VdsoOnly;
        case SyscallId::gettimeofday:
            return SF::VdsoOnly;

        case SyscallId::getpid:
            return SF::ReadOnlyState;
        case SyscallId::getppid:
            return SF::ReadOnlyState;
        case SyscallId::getuid:
            return SF::ReadOnlyState;
        case SyscallId::geteuid:
            return SF::ReadOnlyState;
        case SyscallId::getgid:
            return SF::ReadOnlyState;
        case SyscallId::gettid:
            return SF::ReadOnlyState;
        case SyscallId::uname:
            return SF::ReadOnlyState;
        case SyscallId::sysinfo:
            return SF::ReadOnlyState;

        case SyscallId::open:
            return SF::FileMutation;
        case SyscallId::openat:
            return SF::FileMutation;
        case SyscallId::close:
            return SF::FileMutation;
        case SyscallId::read:
            return SF::FileMutation;
        case SyscallId::write:
            return SF::FileMutation;
        case SyscallId::pread:
            return SF::FileMutation;
        case SyscallId::pwrite:
            return SF::FileMutation;
        case SyscallId::fsync:
            return SF::FileMutation;
        case SyscallId::fdatasync:
            return SF::FileMutation;

        case SyscallId::mmap:
            return SF::MemoryMapping;
        case SyscallId::munmap:
            return SF::MemoryMapping;
        case SyscallId::mprotect:
            return SF::MemoryMapping;
        case SyscallId::madvise:
            return SF::MemoryMapping;

        case SyscallId::futex:
            return SF::ThreadSync;
        case SyscallId::sched_yield:
            return SF::ThreadSync;
        case SyscallId::sched_setaffinity:
            return SF::ThreadSync;
        case SyscallId::sched_setattr:
            return SF::ThreadSync;
        // Reading a scheduler knob touches the same kernel surface as
        // writing one, so the read halves share the writers' family.
        case SyscallId::sched_getaffinity:
            return SF::ThreadSync;
        case SyscallId::sched_getattr:
            return SF::ThreadSync;

        case SyscallId::socket:
            return SF::NetworkIo;
        case SyscallId::connect:
            return SF::NetworkIo;
        case SyscallId::sendmsg:
            return SF::NetworkIo;
        case SyscallId::recvmsg:
            return SF::NetworkIo;

        case SyscallId::clone:
            return SF::ProcessControl;
        case SyscallId::execve:
            return SF::ProcessControl;

        case SyscallId::ptrace:
            return SF::Privilege;
        case SyscallId::capset:
            return SF::Privilege;
        case SyscallId::prctl:
            return SF::Privilege;
        // bpf(2) needs CAP_BPF.  perf_event_open(2) needs CAP_PERFMON or
        // CAP_SYS_ADMIN.
        case SyscallId::bpf:
            return SF::Privilege;
        case SyscallId::perf_event_open:
            return SF::Privilege;

        // Page locking is an attribute of an existing mapping, so it
        // classifies as a mapping operation.
        case SyscallId::mlock2:
            return SF::MemoryMapping;
        case SyscallId::mlock:
            return SF::MemoryMapping;
        case SyscallId::munlock:
            return SF::MemoryMapping;

        // -Werror=switch-default requires this arm.  Privilege is the top
        // of the chain, so a forgotten enumerator errs toward over-
        // restriction.  NoSyscall would put it at the bottom, the most
        // permissive admission, where the misclassification would pass
        // unnoticed.
        default:
            return SF::Privilege;
    }
}

// The SyscallSurface axis admits one engagement per binding.  Two per<> tags,
// or a per<> alongside a family tag, both count as two engagements on that
// axis and are rejected as a duplicate.
template <SyscallId Id>
struct per final : grant_base {};

}  // namespace crucible::fixy::grant::syscall

namespace crucible::fixy::grant {

template <syscall::SyscallId Id>
struct which_dim<syscall::per<Id>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <syscall::SyscallId Id>
struct family_tier<syscall::per<Id>>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily, syscall::family_of(Id)> {};

namespace detail::syscall_per_grant_self_test {

namespace sc = syscall;
namespace al = ::crucible::algebra::lattices;
using D = dim::DimensionAxis;
using SF = al::SyscallFamily;
using SI = sc::SyscallId;

static_assert(IsGrantTag<sc::per<SI::clock_gettime>>);
static_assert(IsGrantTag<sc::per<SI::getpid>>);
static_assert(IsGrantTag<sc::per<SI::write>>);
static_assert(IsGrantTag<sc::per<SI::mmap>>);
static_assert(IsGrantTag<sc::per<SI::futex>>);
static_assert(IsGrantTag<sc::per<SI::socket>>);
static_assert(IsGrantTag<sc::per<SI::clone>>);
static_assert(IsGrantTag<sc::per<SI::ptrace>>);

static_assert(sizeof(sc::per<SI::clock_gettime>) == 1);
static_assert(sizeof(sc::per<SI::getpid>) == 1);
static_assert(sizeof(sc::per<SI::write>) == 1);
static_assert(sizeof(sc::per<SI::mmap>) == 1);
static_assert(sizeof(sc::per<SI::futex>) == 1);
static_assert(sizeof(sc::per<SI::socket>) == 1);
static_assert(sizeof(sc::per<SI::clone>) == 1);
static_assert(sizeof(sc::per<SI::ptrace>) == 1);

static_assert(which_dim_v<sc::per<SI::clock_gettime>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::getpid>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::write>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::mmap>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::futex>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::socket>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::clone>> == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::ptrace>> == D::SyscallSurface);

static_assert(family_tier_v<sc::per<SI::clock_gettime>> == SF::VdsoOnly);
static_assert(family_tier_v<sc::per<SI::clock_getres>> == SF::VdsoOnly);
static_assert(family_tier_v<sc::per<SI::gettimeofday>> == SF::VdsoOnly);
static_assert(family_tier_v<sc::per<SI::getpid>> == SF::ReadOnlyState);
static_assert(family_tier_v<sc::per<SI::geteuid>> == SF::ReadOnlyState);
static_assert(family_tier_v<sc::per<SI::sysinfo>> == SF::ReadOnlyState);
static_assert(family_tier_v<sc::per<SI::open>> == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::write>> == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::pwrite>> == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::fdatasync>> == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::mmap>> == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::mprotect>> == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::madvise>> == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::futex>> == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::sched_yield>> == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::sched_setaffinity>> == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::socket>> == SF::NetworkIo);
static_assert(family_tier_v<sc::per<SI::connect>> == SF::NetworkIo);
static_assert(family_tier_v<sc::per<SI::sendmsg>> == SF::NetworkIo);
static_assert(family_tier_v<sc::per<SI::clone>> == SF::ProcessControl);
static_assert(family_tier_v<sc::per<SI::execve>> == SF::ProcessControl);
static_assert(family_tier_v<sc::per<SI::ptrace>> == SF::Privilege);
static_assert(family_tier_v<sc::per<SI::capset>> == SF::Privilege);

static_assert(family_tier_v<sc::per<SI::sched_setattr>> == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::mlock>> == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::mlock2>> == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::munlock>> == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::prctl>> == SF::Privilege);

static_assert(family_tier_v<sc::per<SI::bpf>> == SF::Privilege);
static_assert(family_tier_v<sc::per<SI::perf_event_open>> == SF::Privilege);

static_assert(!std::is_same_v<sc::per<SI::clock_gettime>, sc::per<SI::getpid>>);
static_assert(!std::is_same_v<sc::per<SI::getpid>, sc::per<SI::write>>);
static_assert(!std::is_same_v<sc::per<SI::write>, sc::per<SI::mmap>>);
static_assert(!std::is_same_v<sc::per<SI::mmap>, sc::per<SI::futex>>);
static_assert(!std::is_same_v<sc::per<SI::futex>, sc::per<SI::socket>>);
static_assert(!std::is_same_v<sc::per<SI::socket>, sc::per<SI::clone>>);
static_assert(!std::is_same_v<sc::per<SI::clone>, sc::per<SI::ptrace>>);
static_assert(!std::is_same_v<sc::per<SI::write>, sc::per<SI::pwrite>>);
static_assert(!std::is_same_v<sc::per<SI::clock_gettime>, sc::per<SI::clock_getres>>);

static_assert(!std::is_same_v<sc::per<SI::write>, sc::family_file_mutation>);
static_assert(!std::is_same_v<sc::per<SI::mmap>, sc::family_memory_mapping>);
static_assert(!std::is_same_v<sc::per<SI::futex>, sc::family_thread_sync>);
static_assert(!std::is_same_v<sc::per<SI::clock_gettime>, sc::family_vdso_only>);

// The covering predicate below plus the cardinality pin together show that
// family_of is total on SyscallId and never reaches the fallback arm.
[[nodiscard]] consteval bool every_syscall_id_classified_correctly() noexcept {
    if (sc::family_of(SI::clock_gettime) != SF::VdsoOnly) return false;
    if (sc::family_of(SI::clock_getres) != SF::VdsoOnly) return false;
    if (sc::family_of(SI::getcpu_vdso) != SF::VdsoOnly) return false;
    if (sc::family_of(SI::gettimeofday) != SF::VdsoOnly) return false;
    if (sc::family_of(SI::getpid) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::getppid) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::getuid) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::geteuid) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::getgid) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::gettid) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::uname) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::sysinfo) != SF::ReadOnlyState) return false;
    if (sc::family_of(SI::open) != SF::FileMutation) return false;
    if (sc::family_of(SI::openat) != SF::FileMutation) return false;
    if (sc::family_of(SI::close) != SF::FileMutation) return false;
    if (sc::family_of(SI::read) != SF::FileMutation) return false;
    if (sc::family_of(SI::write) != SF::FileMutation) return false;
    if (sc::family_of(SI::pread) != SF::FileMutation) return false;
    if (sc::family_of(SI::pwrite) != SF::FileMutation) return false;
    if (sc::family_of(SI::fsync) != SF::FileMutation) return false;
    if (sc::family_of(SI::fdatasync) != SF::FileMutation) return false;
    if (sc::family_of(SI::mmap) != SF::MemoryMapping) return false;
    if (sc::family_of(SI::munmap) != SF::MemoryMapping) return false;
    if (sc::family_of(SI::mprotect) != SF::MemoryMapping) return false;
    if (sc::family_of(SI::madvise) != SF::MemoryMapping) return false;
    if (sc::family_of(SI::futex) != SF::ThreadSync) return false;
    if (sc::family_of(SI::sched_yield) != SF::ThreadSync) return false;
    if (sc::family_of(SI::sched_setaffinity) != SF::ThreadSync) return false;
    if (sc::family_of(SI::sched_setattr) != SF::ThreadSync) return false;
    if (sc::family_of(SI::sched_getaffinity) != SF::ThreadSync) return false;
    if (sc::family_of(SI::sched_getattr) != SF::ThreadSync) return false;
    if (sc::family_of(SI::socket) != SF::NetworkIo) return false;
    if (sc::family_of(SI::connect) != SF::NetworkIo) return false;
    if (sc::family_of(SI::sendmsg) != SF::NetworkIo) return false;
    if (sc::family_of(SI::recvmsg) != SF::NetworkIo) return false;
    if (sc::family_of(SI::clone) != SF::ProcessControl) return false;
    if (sc::family_of(SI::execve) != SF::ProcessControl) return false;
    if (sc::family_of(SI::ptrace) != SF::Privilege) return false;
    if (sc::family_of(SI::capset) != SF::Privilege) return false;
    if (sc::family_of(SI::prctl) != SF::Privilege) return false;
    if (sc::family_of(SI::bpf) != SF::Privilege) return false;
    if (sc::family_of(SI::perf_event_open) != SF::Privilege) return false;
    if (sc::family_of(SI::mlock2) != SF::MemoryMapping) return false;
    if (sc::family_of(SI::mlock) != SF::MemoryMapping) return false;
    if (sc::family_of(SI::munlock) != SF::MemoryMapping) return false;
    return true;
}
static_assert(every_syscall_id_classified_correctly(),
              "family_of(SyscallId) disagrees with the expected tier for at least "
              "one enumerator.  Add the matching case arm, or the classifier "
              "silently returns the Privilege fallback (top of the chain — over-"
              "restrictive but never under-restrictive).");

static constexpr std::size_t syscall_id_count = std::meta::enumerators_of(^^SI).size();
static_assert(syscall_id_count == 45, "SyscallId catalog drifted from the 45-enumerator shipped surface.  "
                                      "A new syscall appends at the next free ordinal AND extends the "
                                      "family_of() switch AND adds an arm to "
                                      "every_syscall_id_classified_correctly().  Reordering or shrinking "
                                      "the enum silently invalidates every stored row_hash federation "
                                      "cache key.");

static_assert(::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
              "family_of() above covers a nine-tier chain.  If the lattice grew "
              "an enumerator, family_of() is incomplete and some SyscallId would "
              "silently fall through to the Privilege fallback.");

}  // namespace detail::syscall_per_grant_self_test

}  // namespace crucible::fixy::grant
