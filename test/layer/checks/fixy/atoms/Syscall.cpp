// The compile-time checks of fixy/atoms/Syscall.h.

#include <fixy/atoms/Syscall.h>

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
        template for (constexpr auto call_member : calls) { known = known || ([:call_member:] == row_id); }
        if (!known) return false;
    }
    return true;
}

// Every family holds exactly one row of the family table.  A family with
// none would take the fallback of syscall_family_effects_of_, and a
// family with two would make its row depend on the order of the table.
[[nodiscard]] consteval bool every_family_has_exactly_one_row_() noexcept {
    bool exact = true;
    static constexpr auto families = std::define_static_array(std::meta::enumerators_of(^^SF));
    template for (constexpr auto family_member : families) {
        constexpr SF family = [:family_member:];
        exact = exact && (syscall_family_rows_naming_(family) == 1);
    }
    return exact;
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
static_assert(std::meta::enumerators_of(^^SI).size() == 50,
              "fixy/atoms/Syscall.h: SyscallId has a different number of calls than this pin records.  A new "
              "call appends at the next free ordinal and raises the count.  A call that disappeared moves "
              "stored federation keys, and somebody has to justify it.");
static_assert(std::meta::enumerators_of(^^SF).size() == 9,
              "fixy/atoms/Syscall.h: the family chain has nine families; a new family needs a row in "
              "syscall_family_effects_table and a place in the chain.");

// Every family has exactly one row, and the table holds no other row.
static_assert(every_family_has_exactly_one_row_(),
              "fixy/atoms/Syscall.h: an enumerator of SyscallFamily holds no row, or two rows, in "
              "syscall_family_effects_table.  Add exactly one row that states its effects.");
static_assert(syscall_family_effects_table.size() == std::meta::enumerators_of(^^SF).size(),
              "fixy/atoms/Syscall.h: the family table and the family chain must have the same size.");

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::syscall, syscall_atom_roster>(),
              "fixy/atoms/Syscall.h: an atom declared in fixy::atom::syscall is missing from syscall_atom_roster.");
static_assert(every_roster_member_is_atom_<syscall_atom_roster>(),
              "fixy/atoms/Syscall.h: a member of syscall_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<syscall_atom_roster, Axis::SyscallSurface>(),
              "fixy/atoms/Syscall.h: every system-call atom engages Axis::SyscallSurface.");
static_assert(every_roster_member_lifts_<syscall_atom_roster>(),
              "fixy/atoms/Syscall.h: every system-call atom lifts to an effect row.");
static_assert(std::tuple_size_v<syscall_atom_roster> == 50 + 9,
              "fixy/atoms/Syscall.h: the roster holds one atom per call and one per family.");
static_assert(std::is_same_v<std::tuple_element_t<41, syscall_atom_roster>, syscall::per<SI::bpf>>);
static_assert(std::is_same_v<std::tuple_element_t<49, syscall_atom_roster>, syscall::per<SI::clock_nanosleep>>);
static_assert(std::is_same_v<std::tuple_element_t<50 + 8, syscall_atom_roster>, syscall::family<SF::Privilege>>);

// The family of a call, read off the atom, at one call per family.
static_assert(syscall::per<SI::clock_gettime>::family == SF::VdsoOnly);
static_assert(syscall::per<SI::getpid>::family == SF::ReadOnlyState);
static_assert(syscall::per<SI::write>::family == SF::FileMutation);
static_assert(syscall::per<SI::mmap>::family == SF::MemoryMapping);
static_assert(syscall::per<SI::mlock2>::family == SF::MemoryMapping);
static_assert(syscall::per<SI::futex>::family == SF::ThreadSync);
static_assert(syscall::per<SI::sched_getattr>::family == SF::ThreadSync);
static_assert(syscall::per<SI::poll>::family == SF::ThreadSync);
static_assert(syscall::per<SI::epoll_wait>::family == SF::ThreadSync);
static_assert(syscall::per<SI::eventfd>::family == SF::ThreadSync);
static_assert(syscall::per<SI::nanosleep>::family == SF::ThreadSync);
static_assert(syscall::per<SI::clock_nanosleep>::family == SF::ThreadSync);
static_assert(syscall::per<SI::socket>::family == SF::NetworkIo);
static_assert(syscall::per<SI::execve>::family == SF::ProcessControl);
static_assert(syscall::per<SI::bpf>::family == SF::Privilege);
static_assert(syscall::per<SI::perf_event_open>::family == SF::Privilege);

// The row, read off the atom.
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::clock_gettime>>, fe::Row<>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::getpid>>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::mmap>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::futex>>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::epoll_wait>>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::clock_nanosleep>>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::per<SI::bpf>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<syscall::family<SF::NoSyscall>>, fe::Row<>>);

// The row of each family, in the canonical form that a hand-written row
// has, so that a lifted row and a stated row are one type.
static_assert(std::is_same_v<syscall_family_row_t<SF::VdsoOnly>, fe::Row<>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::ReadOnlyState>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::FileMutation>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::MemoryMapping>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::ThreadSync>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::NetworkIo>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::ProcessControl>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<syscall_family_row_t<SF::Privilege>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);

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
static_assert(!per_is_admitted_<static_cast<SI>(50)>);
static_assert(family_is_admitted_<SF::Privilege>);
static_assert(!family_is_admitted_<static_cast<SF>(9)>);

// The ordinals that stored keys depend on.
static_assert(std::to_underlying(SI::clock_gettime) == 0);
static_assert(std::to_underlying(SI::bpf) == 41);
static_assert(std::to_underlying(SI::sched_getattr) == 44);
static_assert(std::to_underlying(SI::poll) == 45);
static_assert(std::to_underlying(SI::clock_nanosleep) == 49);

}  // namespace fixy::atom::detail::syscall_atom_self_test
