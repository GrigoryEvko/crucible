#pragma once

// Chain over the breadth of kernel surface a function touches.  bottom
// is NoSyscall and top is Privilege.  Each tier admits the syscall set
// of every tier below it plus its own, and a function declaring a tier
// asserts that its actual syscall set fits inside that tier's set.
//
// join is subset union, which is the propagation reading: a region
// containing one privileged call has the privileged surface.  It is not
// a minimization.  A gate that admits only the narrowest surface tests
// the tier against the floor directly, or takes the meet.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class SyscallFamily : std::uint8_t {
    NoSyscall = 0,  // no kernel transition at all
    VdsoOnly = 1,  // clock_gettime, getcpu through the vDSO — still no kernel transition
    ReadOnlyState = 2,  // read, pread, lseek, fstat, getpid, gettid
    FileMutation = 3,  // write, pwrite, fsync, fdatasync, ftruncate, openat, unlink, renameat2
    MemoryMapping = 4,  // mmap, munmap, mprotect, madvise, mlock, mremap
    ThreadSync = 5,  // futex, eventfd, signalfd, pipe, epoll_*, io_uring_setup, clone for a thread
    NetworkIo = 6,  // socket, bind, connect, send, recv, sendmsg, recvmsg, shutdown, accept4
    ProcessControl = 7,  // fork, vfork, exec, kill, tgkill, sigaction, wait, setrlimit
    Privilege = 8,  // raw ioctl, capset, capget, mount, setuid, ptrace, bpf, kexec, keyctl
};

[[nodiscard]] consteval std::string_view syscall_family_name(SyscallFamily t) noexcept {
    switch (t) {
        case SyscallFamily::NoSyscall:
            return "NoSyscall";
        case SyscallFamily::VdsoOnly:
            return "VdsoOnly";
        case SyscallFamily::ReadOnlyState:
            return "ReadOnlyState";
        case SyscallFamily::FileMutation:
            return "FileMutation";
        case SyscallFamily::MemoryMapping:
            return "MemoryMapping";
        case SyscallFamily::ThreadSync:
            return "ThreadSync";
        case SyscallFamily::NetworkIo:
            return "NetworkIo";
        case SyscallFamily::ProcessControl:
            return "ProcessControl";
        case SyscallFamily::Privilege:
            return "Privilege";
        default:
            return std::string_view{"<unknown SyscallFamily>"};
    }
}

struct SyscallFamilyLattice : ChainLatticeOps<SyscallFamily> {
    [[nodiscard]] static constexpr SyscallFamily bottom() noexcept { return SyscallFamily::NoSyscall; }
    [[nodiscard]] static constexpr SyscallFamily top() noexcept { return SyscallFamily::Privilege; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "SyscallFamilyLattice"; }

    template <SyscallFamily T>
    struct At {
        struct element_type {
            using syscall_family_value_type = SyscallFamily;
            [[nodiscard]] constexpr operator syscall_family_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr SyscallFamily tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case SyscallFamily::NoSyscall:
                    return "SyscallFamilyLattice::At<NoSyscall>";
                case SyscallFamily::VdsoOnly:
                    return "SyscallFamilyLattice::At<VdsoOnly>";
                case SyscallFamily::ReadOnlyState:
                    return "SyscallFamilyLattice::At<ReadOnlyState>";
                case SyscallFamily::FileMutation:
                    return "SyscallFamilyLattice::At<FileMutation>";
                case SyscallFamily::MemoryMapping:
                    return "SyscallFamilyLattice::At<MemoryMapping>";
                case SyscallFamily::ThreadSync:
                    return "SyscallFamilyLattice::At<ThreadSync>";
                case SyscallFamily::NetworkIo:
                    return "SyscallFamilyLattice::At<NetworkIo>";
                case SyscallFamily::ProcessControl:
                    return "SyscallFamilyLattice::At<ProcessControl>";
                case SyscallFamily::Privilege:
                    return "SyscallFamilyLattice::At<Privilege>";
                default:
                    return "SyscallFamilyLattice::At<?>";
            }
        }
    };
};

namespace detail::syscall_family_lattice_self_test {

inline constexpr std::size_t family_count = std::meta::enumerators_of(^^SyscallFamily).size();

static_assert(family_count == 9, "SyscallFamily diverged from {NoSyscall, VdsoOnly, ReadOnlyState, "
                                 "FileMutation, MemoryMapping, ThreadSync, NetworkIo, "
                                 "ProcessControl, Privilege}.  A new family appends at the next "
                                 "free ordinal and needs the matching syscall_family_name() arm "
                                 "and At<T>::name() arm.  Reusing an existing ordinal silently "
                                 "changes every stored row hash.");

static_assert(std::to_underlying(SyscallFamily::NoSyscall) == 0);

static_assert(std::to_underlying(SyscallFamily::Privilege) == 8);

[[nodiscard]] consteval bool every_syscall_family_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SyscallFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = syscall_family_name([:en:]);
        if (n == std::string_view{"<unknown SyscallFamily>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_syscall_family_has_name(), "syscall_family_name() switch missing an arm for at least one "
                                               "SyscallFamily enumerator.  Add the arm or the new family leaks "
                                               "the '<unknown SyscallFamily>' sentinel.");

static_assert(::crucible::algebra::Lattice<SyscallFamilyLattice>);
static_assert(::crucible::algebra::BoundedLattice<SyscallFamilyLattice>);
static_assert(!::crucible::algebra::Semiring<SyscallFamilyLattice>);

static_assert(verify_chain_lattice_exhaustive<SyscallFamilyLattice>(),
              "SyscallFamilyLattice chain-order lattice axioms failed at some "
              "triple — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<SyscallFamilyLattice>(),
              "SyscallFamilyLattice chain failed distributivity check — leq/"
              "join/meet defect.");

static_assert(SyscallFamilyLattice::bottom() == SyscallFamily::NoSyscall);
static_assert(SyscallFamilyLattice::top() == SyscallFamily::Privilege);

static_assert(SyscallFamilyLattice::name() == std::string_view{"SyscallFamilyLattice"});

static_assert(SyscallFamilyLattice::leq(SyscallFamily::NoSyscall, SyscallFamily::Privilege));
static_assert(!SyscallFamilyLattice::leq(SyscallFamily::Privilege, SyscallFamily::NoSyscall));

static_assert(SyscallFamilyLattice::leq(SyscallFamily::NoSyscall, SyscallFamily::VdsoOnly));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::VdsoOnly, SyscallFamily::ReadOnlyState));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::ReadOnlyState, SyscallFamily::FileMutation));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::FileMutation, SyscallFamily::MemoryMapping));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::MemoryMapping, SyscallFamily::ThreadSync));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::ThreadSync, SyscallFamily::NetworkIo));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::NetworkIo, SyscallFamily::ProcessControl));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::ProcessControl, SyscallFamily::Privilege));

static_assert(!SyscallFamilyLattice::leq(SyscallFamily::VdsoOnly, SyscallFamily::NoSyscall));
static_assert(!SyscallFamilyLattice::leq(SyscallFamily::Privilege, SyscallFamily::ProcessControl));

static_assert(SyscallFamilyLattice::join(SyscallFamily::NoSyscall, SyscallFamily::Privilege)
                  == SyscallFamily::Privilege,
              "join returns the widest surface, Privilege.  A consumer that "
              "reads composition as surface minimization would silently admit "
              "the privileged surface.  A gate that wants the narrowest floor "
              "calls meet, or tests against NoSyscall directly.");
static_assert(SyscallFamilyLattice::meet(SyscallFamily::NoSyscall, SyscallFamily::Privilege)
                  == SyscallFamily::NoSyscall,
              "meet returns the narrowest surface, NoSyscall.  An admission gate "
              "that grants only what every participant claims calls meet.");

static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::NoSyscall>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::VdsoOnly>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::ReadOnlyState>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::FileMutation>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::MemoryMapping>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::ThreadSync>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::NetworkIo>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::ProcessControl>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::Privilege>::element_type>);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void syscall_family_lattice_runtime_smoke_test() {
    SyscallFamily a = SyscallFamily::NoSyscall;
    SyscallFamily b = SyscallFamily::Privilege;
    [[maybe_unused]] bool rl = SyscallFamilyLattice::leq(a, b);
    [[maybe_unused]] SyscallFamily rj = SyscallFamilyLattice::join(a, b);
    [[maybe_unused]] SyscallFamily rm = SyscallFamilyLattice::meet(a, b);

    SyscallFamily c = SyscallFamily::FileMutation;
    SyscallFamily d = SyscallFamily::NetworkIo;
    [[maybe_unused]] SyscallFamily rj2 = SyscallFamilyLattice::join(c, d);
    [[maybe_unused]] SyscallFamily rm2 = SyscallFamilyLattice::meet(c, d);

    SyscallFamilyLattice::At<SyscallFamily::ReadOnlyState>::element_type ros_pin{};
    [[maybe_unused]] SyscallFamily ros_recovered = ros_pin;
}

}  // namespace detail::syscall_family_lattice_self_test

}  // namespace crucible::algebra::lattices
