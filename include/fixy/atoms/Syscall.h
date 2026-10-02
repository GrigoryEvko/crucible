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

// How many rows of the table name the call.  Each atom of the roster
// evaluates this count in each includer.  A read through a pointer is the
// cheapest read of a constant evaluation, so the loop reads the rows
// through one.  Complexity: linear in the table.
[[nodiscard]] consteval std::size_t syscall_rows_naming_(sc::SyscallId id) noexcept {
    const std::pair<sc::SyscallId, sc::SyscallFamily>* const table_rows = syscall_family_table.data();
    const std::size_t row_count = syscall_family_table.size();
    std::size_t rows = 0;
    for (std::size_t place = 0; place < row_count; ++place) {
        if (table_rows[place].first == id) ++rows;
    }
    return rows;
}

// The family of a call.  The check file of this header proves that
// every call holds exactly one row, so the loop always returns from
// inside.  The value
// after it is the top of the chain, the most restrictive answer, and it
// is there only because a function must end in a return.
[[nodiscard]] consteval sc::SyscallFamily syscall_family_of_(sc::SyscallId id) noexcept {
    const std::pair<sc::SyscallId, sc::SyscallFamily>* const table_rows = syscall_family_table.data();
    const std::size_t row_count = syscall_family_table.size();
    for (std::size_t place = 0; place < row_count; ++place) {
        if (table_rows[place].first == id) return table_rows[place].second;
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

// How many rows of the family table name the family.  The loop reads
// the rows through a pointer, as the count of the calls does.
// Complexity: linear in the table.
[[nodiscard]] consteval std::size_t syscall_family_rows_naming_(sc::SyscallFamily family) noexcept {
    const std::pair<sc::SyscallFamily, syscall_family_effects>* const table_rows = syscall_family_effects_table.data();
    const std::size_t row_count = syscall_family_effects_table.size();
    std::size_t rows = 0;
    for (std::size_t place = 0; place < row_count; ++place) {
        if (table_rows[place].first == family) ++rows;
    }
    return rows;
}

// The effects of a family.  A family with no row gets IO and Block, the
// most restrictive answer, and the check file of this header refuses
// such a family.
[[nodiscard]] consteval syscall_family_effects syscall_family_effects_of_(sc::SyscallFamily family) noexcept {
    const std::pair<sc::SyscallFamily, syscall_family_effects>* const table_rows = syscall_family_effects_table.data();
    const std::size_t row_count = syscall_family_effects_table.size();
    for (std::size_t place = 0; place < row_count; ++place) {
        if (table_rows[place].first == family) return table_rows[place].second;
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

// A value of SyscallFamily that names a family of the chain: a value
// that holds exactly one row of the family table.  The check file of
// this header proves that each enumerator holds one row and that the
// table has one row for each enumerator, so these values are the
// enumerators.  A cast from an integer can make any value of the
// underlying type, and such a value holds no row.
[[nodiscard]] consteval bool is_catalogued_family_(sc::SyscallFamily family) noexcept {
    return syscall_family_rows_naming_(family) == 1;
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

// Every call, in the order of the enumerators.  The atom is a template,
// and the namespace walk of every_atom_in_is_rostered_ sees no
// instantiation of a template.  So the check file of this header walks
// the enumerators of SyscallId and requires each to hold exactly one atom
// of this list.  A new call with no atom here fails the build.
using syscall_call_atom_roster = std::tuple<
    syscall::per<sc::SyscallId::clock_gettime>, syscall::per<sc::SyscallId::clock_getres>,
    syscall::per<sc::SyscallId::getcpu_vdso>, syscall::per<sc::SyscallId::gettimeofday>,
    syscall::per<sc::SyscallId::getpid>, syscall::per<sc::SyscallId::getppid>, syscall::per<sc::SyscallId::getuid>,
    syscall::per<sc::SyscallId::geteuid>, syscall::per<sc::SyscallId::getgid>, syscall::per<sc::SyscallId::gettid>,
    syscall::per<sc::SyscallId::uname>, syscall::per<sc::SyscallId::sysinfo>, syscall::per<sc::SyscallId::open>,
    syscall::per<sc::SyscallId::openat>, syscall::per<sc::SyscallId::close>, syscall::per<sc::SyscallId::read>,
    syscall::per<sc::SyscallId::write>, syscall::per<sc::SyscallId::pread>, syscall::per<sc::SyscallId::pwrite>,
    syscall::per<sc::SyscallId::fsync>, syscall::per<sc::SyscallId::fdatasync>, syscall::per<sc::SyscallId::mmap>,
    syscall::per<sc::SyscallId::munmap>, syscall::per<sc::SyscallId::mprotect>, syscall::per<sc::SyscallId::madvise>,
    syscall::per<sc::SyscallId::futex>, syscall::per<sc::SyscallId::sched_yield>,
    syscall::per<sc::SyscallId::sched_setaffinity>, syscall::per<sc::SyscallId::socket>,
    syscall::per<sc::SyscallId::connect>, syscall::per<sc::SyscallId::sendmsg>, syscall::per<sc::SyscallId::recvmsg>,
    syscall::per<sc::SyscallId::clone>, syscall::per<sc::SyscallId::execve>, syscall::per<sc::SyscallId::ptrace>,
    syscall::per<sc::SyscallId::capset>, syscall::per<sc::SyscallId::sched_setattr>,
    syscall::per<sc::SyscallId::mlock2>, syscall::per<sc::SyscallId::mlock>, syscall::per<sc::SyscallId::munlock>,
    syscall::per<sc::SyscallId::prctl>, syscall::per<sc::SyscallId::bpf>, syscall::per<sc::SyscallId::perf_event_open>,
    syscall::per<sc::SyscallId::sched_getaffinity>, syscall::per<sc::SyscallId::sched_getattr>,
    syscall::per<sc::SyscallId::poll>, syscall::per<sc::SyscallId::epoll_wait>, syscall::per<sc::SyscallId::eventfd>,
    syscall::per<sc::SyscallId::nanosleep>, syscall::per<sc::SyscallId::clock_nanosleep>>;

// Every family, in the order of the chain.  The check file holds this
// list to the enumerators of SyscallFamily in the same way.
using syscall_family_atom_roster =
    std::tuple<syscall::family<sc::SyscallFamily::NoSyscall>, syscall::family<sc::SyscallFamily::VdsoOnly>,
               syscall::family<sc::SyscallFamily::ReadOnlyState>, syscall::family<sc::SyscallFamily::FileMutation>,
               syscall::family<sc::SyscallFamily::MemoryMapping>, syscall::family<sc::SyscallFamily::ThreadSync>,
               syscall::family<sc::SyscallFamily::NetworkIo>, syscall::family<sc::SyscallFamily::ProcessControl>,
               syscall::family<sc::SyscallFamily::Privilege>>;

// Every call and every family.  The two lists name each atom, so an
// includer evaluates the constraint of each atom and no reflection.
using syscall_atom_roster = roster_cat_t<syscall_call_atom_roster, syscall_family_atom_roster>;

}  // namespace fixy::atom::detail
