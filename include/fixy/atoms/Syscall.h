#pragma once

// The system-call atoms: which system calls a binding issues.  Every
// atom here engages Axis::SyscallSurface and lifts to the effect row of
// the family its call belongs to.
//
// per<Id> names one call.  family<F> names a whole family, for a site
// that issues several calls of one family and wants to say so once.
//
// Old spelling: include/crucible/fixy/syscall/Per.h (the per grant and
// the SyscallId catalog), include/crucible/fixy/syscall/Family.h (the
// family grants) and include/crucible/fixy/syscall/Bridge.h (the map
// from a family to a row).
//
// ---------------------------------------------------------------------
// The enums live here, and why
//
// foundation ports no SyscallFamily lattice.  The old one at
// include/crucible/algebra/lattices/SyscallFamilyLattice.h is not carried
// across.  The atoms need its nine enumerators and their order, so they
// are declared here with the same names and the same values, the way
// fixy/atoms/Hw.h declares the instruction tiers.  A later foundation
// port can alias to these or these to it.  The values are the contract
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
// The old classifier was a switch with a default arm that returned
// Privilege.  A call with no arm then fell to the top of the chain, and
// only a hand-written list of every enumerator noticed.  Here the family
// of a call is one row of a table, and the self-test walks the
// enumerators of SyscallId by reflection and requires each to hold
// exactly one row.  A new call with no row fails the build.
//
// ---------------------------------------------------------------------
// The row of a family
//
// The map is the old bridge's, with one change.  A mapping call lifts to
// IO and Block, not to IO alone.  fixy/atoms/Os.h makes the same
// decision for its mmap atoms: the old gate that reached a real mapping
// call asked for both, because a mapping can park the caller on page
// cache pressure or on a remote page fault.

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
// family below it.  Old spelling:
// crucible::algebra::lattices::SyscallFamily.
enum class SyscallFamily : std::uint8_t {
    NoSyscall = 0,  // no kernel transition at all
    VdsoOnly = 1,  // clock_gettime and getcpu through the vDSO, still no kernel transition
    ReadOnlyState = 2,  // getpid, gettid, uname: reads of process state
    FileMutation = 3,  // open, read, write, fsync: the file surface
    MemoryMapping = 4,  // mmap, munmap, mprotect, madvise, mlock
    ThreadSync = 5,  // futex, sched_yield, the scheduler knobs
    NetworkIo = 6,  // socket, connect, sendmsg, recvmsg
    ProcessControl = 7,  // clone, execve
    Privilege = 8,  // ptrace, capset, prctl, bpf, perf_event_open
};

// Old spelling: crucible::fixy::grant::syscall::SyscallId.  The
// ordinals are frozen; the head of this file says why.
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
};

}  // namespace fixy::atom::syscall

namespace fixy::atom::detail {

namespace sc = ::fixy::atom::syscall;

// One row per call.  The self-test below requires each enumerator of
// SyscallId to hold exactly one row.
inline constexpr std::array<std::pair<sc::SyscallId, sc::SyscallFamily>, 45> syscall_family_table{{
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

// The family of a call.  The self-test proves that every call holds
// exactly one row, so the loop always returns from inside.  The value
// after it is the top of the chain, the most restrictive answer, and it
// is there only because a function must end in a return.
[[nodiscard]] consteval sc::SyscallFamily syscall_family_of_(sc::SyscallId id) noexcept {
    for (const auto& [row_id, row_family] : syscall_family_table) {
        if (row_id == id) return row_family;
    }
    return sc::SyscallFamily::Privilege;
}

template <sc::SyscallFamily F>
struct syscall_family_row;

template <>
struct syscall_family_row<sc::SyscallFamily::NoSyscall> {
    using type = ::foundation::effects::Row<>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::VdsoOnly> {
    using type = ::foundation::effects::Row<>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::ReadOnlyState> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::FileMutation> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::MemoryMapping> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::ThreadSync> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::Block>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::NetworkIo> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::ProcessControl> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};
template <>
struct syscall_family_row<sc::SyscallFamily::Privilege> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};

template <sc::SyscallFamily F>
using syscall_family_row_t = typename syscall_family_row<F>::type;

// A value of SyscallId that names a call of the catalog.  A cast from an
// integer can make any value of the underlying type, and such a value
// holds no row.
[[nodiscard]] consteval bool is_catalogued_call_(sc::SyscallId id) noexcept {
    return syscall_rows_naming_(id) == 1;
}

// A value of SyscallFamily that names a family of the chain.
[[nodiscard]] consteval bool is_catalogued_family_(sc::SyscallFamily family) noexcept {
    bool known = false;
    static constexpr auto families = std::define_static_array(std::meta::enumerators_of(^^sc::SyscallFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto family_member : families) {
        known = known || ([:family_member:] == family);
    }
#pragma GCC diagnostic pop
    return known;
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
inline constexpr auto syscall_families_ =
    std::define_static_array(std::meta::enumerators_of(^^syscall::SyscallFamily));

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

namespace fixy::atom::detail::syscall_atom_self_test {

namespace fe = ::foundation::effects;
using SF = syscall::SyscallFamily;
using SI = syscall::SyscallId;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// Every call holds exactly one row: a call with none would fall to the
// value after the loop in syscall_family_of_, and a call with two would
// make the family depend on the order of the table.
[[nodiscard]] consteval bool every_call_has_exactly_one_row_() noexcept {
    bool exact = true;
    static constexpr auto calls = std::define_static_array(std::meta::enumerators_of(^^SI));
    template for (constexpr auto call_member : calls) {
        constexpr SI call = [:call_member:];
        exact = exact && (syscall_rows_naming_(call) == 1);
    }
    return exact;
}

// Every row of the table names a call that exists.  A row for a value
// outside the enumerators would pass the check above and name nothing.
[[nodiscard]] consteval bool every_row_names_a_call_() noexcept {
    static constexpr auto calls = std::define_static_array(std::meta::enumerators_of(^^SI));
    for (const auto& [row_id, row_family] : syscall_family_table) {
        bool known = false;
        template for (constexpr auto call_member : calls) {
            known = known || ([:call_member:] == row_id);
        }
        if (!known) return false;
    }
    return true;
}

#pragma GCC diagnostic pop

static_assert(every_call_has_exactly_one_row_(),
              "fixy/atoms/Syscall.h: an enumerator of SyscallId holds no row, or two rows, in "
              "syscall_family_table.  Add exactly one row that names its family.");
static_assert(every_row_names_a_call_(),
              "fixy/atoms/Syscall.h: a row of syscall_family_table names a value that is not an enumerator "
              "of SyscallId.");
static_assert(syscall_family_table.size() == std::meta::enumerators_of(^^SI).size(),
              "fixy/atoms/Syscall.h: the table and the catalog must have the same size.");

// The catalog is append-only.  A new call raises this count.
static_assert(std::meta::enumerators_of(^^SI).size() == 45,
              "fixy/atoms/Syscall.h: SyscallId has a different number of calls than this pin records.  A new "
              "call appends at the next free ordinal and raises the count.  A call that disappeared moves "
              "stored federation keys, and somebody has to justify it.");
static_assert(std::meta::enumerators_of(^^SF).size() == 9,
              "fixy/atoms/Syscall.h: the family chain has nine families; a new family needs a row in "
              "syscall_family_row and a place in the chain.");

// Every family has a row.
static_assert(fe::every_enumerator_lifted<SF, syscall_family_row>(),
              "fixy/atoms/Syscall.h: a SyscallFamily has no specialization of syscall_family_row.");

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::syscall, syscall_atom_roster>(),
              "fixy/atoms/Syscall.h: an atom declared in fixy::atom::syscall is missing from syscall_atom_roster.");
static_assert(every_roster_member_is_atom_<syscall_atom_roster>(),
              "fixy/atoms/Syscall.h: a member of syscall_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<syscall_atom_roster, Axis::SyscallSurface>(),
              "fixy/atoms/Syscall.h: every system-call atom engages Axis::SyscallSurface.");
static_assert(every_roster_member_lifts_<syscall_atom_roster>(),
              "fixy/atoms/Syscall.h: every system-call atom lifts to an effect row.");
static_assert(std::tuple_size_v<syscall_atom_roster> == 45 + 9,
              "fixy/atoms/Syscall.h: the roster holds one atom per call and one per family.");
static_assert(std::is_same_v<std::tuple_element_t<41, syscall_atom_roster>, syscall::per<SI::bpf>>);
static_assert(std::is_same_v<std::tuple_element_t<45 + 8, syscall_atom_roster>, syscall::family<SF::Privilege>>);

// The family of a call, read off the atom, at one call per family.
static_assert(syscall::per<SI::clock_gettime>::family == SF::VdsoOnly);
static_assert(syscall::per<SI::getpid>::family == SF::ReadOnlyState);
static_assert(syscall::per<SI::write>::family == SF::FileMutation);
static_assert(syscall::per<SI::mmap>::family == SF::MemoryMapping);
static_assert(syscall::per<SI::mlock2>::family == SF::MemoryMapping);
static_assert(syscall::per<SI::futex>::family == SF::ThreadSync);
static_assert(syscall::per<SI::sched_getattr>::family == SF::ThreadSync);
static_assert(syscall::per<SI::socket>::family == SF::NetworkIo);
static_assert(syscall::per<SI::execve>::family == SF::ProcessControl);
static_assert(syscall::per<SI::bpf>::family == SF::Privilege);
static_assert(syscall::per<SI::perf_event_open>::family == SF::Privilege);

// The row, read off the atom.
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::clock_gettime>>, fe::Row<>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::getpid>>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::mmap>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::futex>>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::bpf>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::family<SF::NoSyscall>>, fe::Row<>>);

// A call and a family are distinct atoms, and two calls are distinct.
static_assert(!std::is_same_v<syscall::per<SI::write>, syscall::per<SI::pwrite>>);
static_assert(!std::is_same_v<syscall::per<SI::mmap>, syscall::family<SF::MemoryMapping>>);
static_assert(syscall::family<SF::ThreadSync>::tier == SF::ThreadSync);

// A value outside the catalog is refused by both atoms.
template <SI Id>
concept per_is_admitted_ = requires { typename syscall::per<Id>; };
template <SF F>
concept family_is_admitted_ = requires { typename syscall::family<F>; };
static_assert(per_is_admitted_<SI::bpf>);
static_assert(!per_is_admitted_<static_cast<SI>(45)>);
static_assert(family_is_admitted_<SF::Privilege>);
static_assert(!family_is_admitted_<static_cast<SF>(9)>);

// The ordinals that stored keys depend on.
static_assert(std::to_underlying(SI::clock_gettime) == 0);
static_assert(std::to_underlying(SI::bpf) == 41);
static_assert(std::to_underlying(SI::sched_getattr) == 44);

}  // namespace fixy::atom::detail::syscall_atom_self_test
