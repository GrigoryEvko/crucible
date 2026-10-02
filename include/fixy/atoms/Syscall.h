#pragma once

// The system-call atoms: which system calls a binding issues.  Every
// atom here engages Axis::SyscallSurface and lifts to the effect row of
// the family its call belongs to.
//
// per<Id> names one call.  family<F> names a whole family, for a site
// that issues several calls of one family and wants to say so once.
//
// ---------------------------------------------------------------------
// The enums live here, and why
//
// foundation has no SyscallFamily lattice.  The atoms need nine families
// and their order, so this header declares them, the way fixy/atoms/Hw.h
// declares the instruction tiers.  A lattice that foundation adds later
// can alias to these, or these to it.  The values are the contract
// either way.
//
// The ordinals of SyscallId are frozen.  The stable identity of an atom
// folds the value of its template argument, and that identity is part
// of a federation cache key.  A new call appends at the next free
// ordinal.  Reusing or reordering one moves every stored key, and
// nothing diagnoses it.
//
// ---------------------------------------------------------------------
// The family of a call is a table
//
// A switch with a default arm would send a call that has no arm to the
// top of the chain, and only a hand-written list of every enumerator
// would notice.  So the family of a call is one row of a table.  The
// check file of this header walks the enumerators of SyscallId by
// reflection and requires each to hold exactly one row.  A new call
// with no row fails the build.
//
// ---------------------------------------------------------------------
// The row of a family
//
// syscall_family_effects_table gives the row of each family.  A mapping
// call lifts to IO and Block, not to IO alone, because a mapping can park
// the caller on page cache pressure or on a remote page fault.
// fixy/atoms/Os.h makes the same decision for its mmap atoms.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::atom::syscall {

inline constexpr atom_seal atom_namespace_seal{};

// A chain.  Each family stands for a wider kernel surface than the
// family below it.
enum class SyscallFamily : std::uint8_t {
    NoSyscall = 0,  // no kernel transition at all
    VdsoOnly = 1,  // clock_gettime and getcpu through the vDSO, still no kernel transition
    ReadOnlyState = 2,  // getpid, gettid, uname: reads of process state
    FileMutation = 3,  // open, read, write, fsync: the file surface
    MemoryMapping = 4,  // mmap, munmap, mprotect, madvise, mlock
    ThreadSync = 5,  // futex, sched_yield, the scheduler knobs, poll, epoll_wait, the sleeps
    NetworkIo = 6,  // socket, connect, sendmsg, recvmsg
    ProcessControl = 7,  // clone, execve
    Privilege = 8,  // ptrace, capset, prctl, bpf, perf_event_open
};

// The ordinals are frozen.  The head of this file says why.
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

    // The read halves of the two scheduler knobs above.  A hardening
    // pass reads the prior affinity mask and the prior scheduling
    // attributes before it writes new ones.
    sched_getaffinity = 43,
    sched_getattr = 44,

    // The waits on a descriptor, the sleeps, and eventfd, which makes a
    // descriptor for such a wait.  The table below files each one in
    // ThreadSync, so each lifts Block.
    poll = 45,
    epoll_wait = 46,
    eventfd = 47,
    nanosleep = 48,
    clock_nanosleep = 49,
};

}  // namespace fixy::atom::syscall

namespace fixy::atom::detail {

namespace sc = ::fixy::atom::syscall;

// One row per call.  The check file of this header requires each
// enumerator of SyscallId to hold exactly one row.
inline constexpr std::array<std::pair<sc::SyscallId, sc::SyscallFamily>, 50> syscall_family_table{{
    {sc::SyscallId::clock_gettime, sc::SyscallFamily::VdsoOnly},
    {sc::SyscallId::clock_getres, sc::SyscallFamily::VdsoOnly},
    {sc::SyscallId::getcpu_vdso, sc::SyscallFamily::VdsoOnly},
    {sc::SyscallId::gettimeofday, sc::SyscallFamily::VdsoOnly},

    {sc::SyscallId::getpid, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::getppid, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::getuid, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::geteuid, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::getgid, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::gettid, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::uname, sc::SyscallFamily::ReadOnlyState},
    {sc::SyscallId::sysinfo, sc::SyscallFamily::ReadOnlyState},

    {sc::SyscallId::open, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::openat, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::close, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::read, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::write, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::pread, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::pwrite, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::fsync, sc::SyscallFamily::FileMutation},
    {sc::SyscallId::fdatasync, sc::SyscallFamily::FileMutation},

    {sc::SyscallId::mmap, sc::SyscallFamily::MemoryMapping},
    {sc::SyscallId::munmap, sc::SyscallFamily::MemoryMapping},
    {sc::SyscallId::mprotect, sc::SyscallFamily::MemoryMapping},
    {sc::SyscallId::madvise, sc::SyscallFamily::MemoryMapping},
    // Page locking is an attribute of an existing mapping, so it is a
    // mapping call.
    {sc::SyscallId::mlock2, sc::SyscallFamily::MemoryMapping},
    {sc::SyscallId::mlock, sc::SyscallFamily::MemoryMapping},
    {sc::SyscallId::munlock, sc::SyscallFamily::MemoryMapping},

    {sc::SyscallId::futex, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::sched_yield, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::sched_setaffinity, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::sched_setattr, sc::SyscallFamily::ThreadSync},
    // A read of a scheduler knob touches the same kernel surface as a
    // write of it.
    {sc::SyscallId::sched_getaffinity, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::sched_getattr, sc::SyscallFamily::ThreadSync},
    // A wait on a descriptor and a sleep each park the caller until an
    // event or a deadline.  eventfd makes the descriptor that such a wait
    // reads, so the table files it with the waits.
    {sc::SyscallId::poll, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::epoll_wait, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::eventfd, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::nanosleep, sc::SyscallFamily::ThreadSync},
    {sc::SyscallId::clock_nanosleep, sc::SyscallFamily::ThreadSync},

    {sc::SyscallId::socket, sc::SyscallFamily::NetworkIo},
    {sc::SyscallId::connect, sc::SyscallFamily::NetworkIo},
    {sc::SyscallId::sendmsg, sc::SyscallFamily::NetworkIo},
    {sc::SyscallId::recvmsg, sc::SyscallFamily::NetworkIo},

    {sc::SyscallId::clone, sc::SyscallFamily::ProcessControl},
    {sc::SyscallId::execve, sc::SyscallFamily::ProcessControl},

    {sc::SyscallId::ptrace, sc::SyscallFamily::Privilege},
    {sc::SyscallId::capset, sc::SyscallFamily::Privilege},
    {sc::SyscallId::prctl, sc::SyscallFamily::Privilege},
    // bpf(2) needs CAP_BPF.  perf_event_open(2) needs CAP_PERFMON or
    // CAP_SYS_ADMIN.
    {sc::SyscallId::bpf, sc::SyscallFamily::Privilege},
    {sc::SyscallId::perf_event_open, sc::SyscallFamily::Privilege},
}};

// How many rows of the table name the call.  Complexity: linear in the
// table.
[[nodiscard]] consteval std::size_t syscall_rows_naming_(sc::SyscallId id) noexcept {
    std::size_t rows = 0;
    for (const auto& [row_id, row_family] : syscall_family_table) {
        if (row_id == id) ++rows;
    }
    return rows;
}

// The family of a call.  The check file of this header proves that
// every call holds exactly one row, so the loop always returns from
// inside.  The value
// after it is the top of the chain, the most restrictive answer, and it
// is there only because a function must end in a return.
[[nodiscard]] consteval sc::SyscallFamily syscall_family_of_(sc::SyscallId id) noexcept {
    for (const auto& [row_id, row_family] : syscall_family_table) {
        if (row_id == id) return row_family;
    }
    return sc::SyscallFamily::Privilege;
}

// The effects that a family of calls needs from its context.
struct syscall_family_effects {
    bool needs_io = true;
    bool needs_block = true;
};

// One row per family.  The check file of this header requires each
// enumerator of SyscallFamily to hold exactly one row, as it does for
// the calls.
inline constexpr std::array<std::pair<sc::SyscallFamily, syscall_family_effects>, 9> syscall_family_effects_table{{
    {sc::SyscallFamily::NoSyscall, {.needs_io = false, .needs_block = false}},
    {sc::SyscallFamily::VdsoOnly, {.needs_io = false, .needs_block = false}},
    {sc::SyscallFamily::ReadOnlyState, {.needs_io = true, .needs_block = false}},
    {sc::SyscallFamily::FileMutation, {.needs_io = true, .needs_block = true}},
    {sc::SyscallFamily::MemoryMapping, {.needs_io = true, .needs_block = true}},
    {sc::SyscallFamily::ThreadSync, {.needs_io = false, .needs_block = true}},
    {sc::SyscallFamily::NetworkIo, {.needs_io = true, .needs_block = true}},
    {sc::SyscallFamily::ProcessControl, {.needs_io = true, .needs_block = true}},
    {sc::SyscallFamily::Privilege, {.needs_io = true, .needs_block = true}},
}};

// How many rows of the family table name the family.  Complexity: linear
// in the table.
[[nodiscard]] consteval std::size_t syscall_family_rows_naming_(sc::SyscallFamily family) noexcept {
    std::size_t rows = 0;
    for (const auto& [row_family, row_effects] : syscall_family_effects_table) {
        if (row_family == family) ++rows;
    }
    return rows;
}

// The effects of a family.  A family with no row gets IO and Block, the
// most restrictive answer, and the check file of this header refuses
// such a family.
[[nodiscard]] consteval syscall_family_effects syscall_family_effects_of_(sc::SyscallFamily family) noexcept {
    for (const auto& [row_family, row_effects] : syscall_family_effects_table) {
        if (row_family == family) return row_effects;
    }
    return syscall_family_effects{};
}

template <sc::SyscallFamily F>
using syscall_family_row_t = ::foundation::effects::row_union_t<
    std::conditional_t<syscall_family_effects_of_(F).needs_io,
                       ::foundation::effects::Row<::foundation::effects::Effect::IO>, ::foundation::effects::Row<>>,
    std::conditional_t<syscall_family_effects_of_(F).needs_block,
                       ::foundation::effects::Row<::foundation::effects::Effect::Block>, ::foundation::effects::Row<>>>;

// A value of SyscallId that names a call of the catalog.  A cast from an
// integer can make any value of the underlying type, and such a value
// holds no row.
[[nodiscard]] consteval bool is_catalogued_call_(sc::SyscallId id) noexcept { return syscall_rows_naming_(id) == 1; }

// A value of SyscallFamily that names a family of the chain.  The loop
// runs only when a call evaluates it.
[[nodiscard]] consteval bool is_catalogued_family_(sc::SyscallFamily family) noexcept {
    for (const std::meta::info family_member : std::meta::enumerators_of(^^sc::SyscallFamily)) {
        if (std::meta::extract<sc::SyscallFamily>(std::meta::constant_of(family_member)) == family) return true;
    }
    return false;
}

}  // namespace fixy::atom::detail

namespace fixy::atom::syscall {

// One call.  The family and the row come from the table, so a site names
// only the call.  A value that names no call of the catalog is refused,
// so the value after the loop in syscall_family_of_ is never reached.
template <SyscallId Id>
    requires(::fixy::atom::detail::is_catalogued_call_(Id))
struct per final
    : lifting_atom_of<Axis::SyscallSurface,
                      ::fixy::atom::detail::syscall_family_row_t<::fixy::atom::detail::syscall_family_of_(Id)>> {
    static constexpr SyscallId id = Id;
    static constexpr SyscallFamily family = ::fixy::atom::detail::syscall_family_of_(Id);
};

// A whole family.  A value that names no family of the chain is refused.
template <SyscallFamily F>
    requires(::fixy::atom::detail::is_catalogued_family_(F))
struct family final : lifting_atom_of<Axis::SyscallSurface, ::fixy::atom::detail::syscall_family_row_t<F>> {
    static constexpr SyscallFamily tier = F;
};

}  // namespace fixy::atom::syscall

namespace fixy::atom::detail {

inline constexpr auto syscall_calls_ = std::define_static_array(std::meta::enumerators_of(^^syscall::SyscallId));
inline constexpr auto syscall_families_ = std::define_static_array(std::meta::enumerators_of(^^syscall::SyscallFamily));

// Declared and never defined: only its return type is read.
template <std::size_t... Call, std::size_t... Family>
auto syscall_roster_of_(std::index_sequence<Call...>, std::index_sequence<Family...>)
    -> std::tuple<syscall::per<std::meta::extract<syscall::SyscallId>(syscall_calls_[Call])>...,
                  syscall::family<std::meta::extract<syscall::SyscallFamily>(syscall_families_[Family])>...>;

// Every call and every family.  The two atoms are templates, and the
// namespace walk of every_atom_in_is_rostered_ sees no instantiation of a
// template.  The roster is read off the two enums for that reason: a new
// call is in the roster the day it is declared, with no hand list to
// forget.
using syscall_atom_roster = decltype(syscall_roster_of_(std::make_index_sequence<syscall_calls_.size()>{},
                                                        std::make_index_sequence<syscall_families_.size()>{}));

}  // namespace fixy::atom::detail
