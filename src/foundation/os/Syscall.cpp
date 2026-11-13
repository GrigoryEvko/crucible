// The door of the raw system call primitive (foundation/os/Syscall.h) and of
// the kernel floor (foundation/os/KernelFloor.h).
//
// This file is the one place of the base with the system call instruction,
// and the one includer of <asm/unistd.h>, <asm-generic/errno-base.h>,
// <linux/fcntl.h>, <linux/futex.h>, <linux/mempolicy.h>, <linux/mman.h>,
// <linux/resource.h>, <linux/sched.h>, <linux/sched/types.h>,
// <linux/time_types.h> and <linux/utsname.h>.  It includes no header of the
// C library, because some declarations of the two kinds of header conflict.
// In a build with ThreadSanitizer, the primitive gives a read or a write that
// can block to src/foundation/os/Intercepted.cpp, which is the one door of the
// floor that calls the C library.  It issues a futex call raw, between the
// hooks of that file.

#include <foundation/os/FutexWords.h>
#include <foundation/os/KernelFloor.h>
#include <foundation/os/MapWords.h>
#include <foundation/os/MemoryPolicyWords.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/SchedWords.h>
#include <foundation/os/Syscall.h>

#include <asm-generic/errno-base.h>
#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/mempolicy.h>
#include <linux/mman.h>
#include <linux/resource.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/time_types.h>
#include <linux/utsname.h>

#include <cstddef>
#include <cstdint>
#include <meta>

#if defined(__SANITIZE_THREAD__)
#include "Intercepted.h"
#endif

namespace foundation::os {

// The argument words of foundation/os/OpenFlags.h are the words of the kernel
// header.
static_assert(kAtCurrentDirectory == AT_FDCWD, "kAtCurrentDirectory is not AT_FDCWD of <linux/fcntl.h>");
static_assert(kAtSymlinkNoFollow == AT_SYMLINK_NOFOLLOW,
              "kAtSymlinkNoFollow is not AT_SYMLINK_NOFOLLOW of <linux/fcntl.h>");
static_assert(kOpenReadOnly == O_RDONLY, "kOpenReadOnly is not O_RDONLY of <linux/fcntl.h>");
static_assert(kOpenWriteOnly == O_WRONLY, "kOpenWriteOnly is not O_WRONLY of <linux/fcntl.h>");
static_assert(kOpenReadWrite == O_RDWR, "kOpenReadWrite is not O_RDWR of <linux/fcntl.h>");
static_assert(kOpenCreate == O_CREAT, "kOpenCreate is not O_CREAT of <linux/fcntl.h>");
static_assert(kOpenTruncate == O_TRUNC, "kOpenTruncate is not O_TRUNC of <linux/fcntl.h>");
static_assert(kOpenAppend == O_APPEND, "kOpenAppend is not O_APPEND of <linux/fcntl.h>");
static_assert(kOpenDataSync == O_DSYNC, "kOpenDataSync is not O_DSYNC of <linux/fcntl.h>");
static_assert(kOpenFullSync == O_SYNC, "kOpenFullSync is not O_SYNC of <linux/fcntl.h>");
static_assert(kOpenCloseOnExec == O_CLOEXEC, "kOpenCloseOnExec is not O_CLOEXEC of <linux/fcntl.h>");
static_assert(kOpenDirectory == O_DIRECTORY, "kOpenDirectory is not O_DIRECTORY of <linux/fcntl.h>");
static_assert(kOpenNoFollow == O_NOFOLLOW, "kOpenNoFollow is not O_NOFOLLOW of <linux/fcntl.h>");
static_assert(kOpenDirect == O_DIRECT, "kOpenDirect is not O_DIRECT of <linux/fcntl.h>");

// The argument words of foundation/os/MapWords.h are the words of the kernel
// header.
static_assert(kProtectNone == PROT_NONE, "kProtectNone is not PROT_NONE of <linux/mman.h>");
static_assert(kProtectRead == PROT_READ, "kProtectRead is not PROT_READ of <linux/mman.h>");
static_assert(kProtectWrite == PROT_WRITE, "kProtectWrite is not PROT_WRITE of <linux/mman.h>");
static_assert(kProtectExecute == PROT_EXEC, "kProtectExecute is not PROT_EXEC of <linux/mman.h>");
static_assert(kMapShared == MAP_SHARED, "kMapShared is not MAP_SHARED of <linux/mman.h>");
static_assert(kMapPrivate == MAP_PRIVATE, "kMapPrivate is not MAP_PRIVATE of <linux/mman.h>");
static_assert(kMapAnonymous == MAP_ANONYMOUS, "kMapAnonymous is not MAP_ANONYMOUS of <linux/mman.h>");
static_assert(kMapLocked == MAP_LOCKED, "kMapLocked is not MAP_LOCKED of <linux/mman.h>");
static_assert(kMapPopulate == MAP_POPULATE, "kMapPopulate is not MAP_POPULATE of <linux/mman.h>");
static_assert(kMapHugeTlb == MAP_HUGETLB, "kMapHugeTlb is not MAP_HUGETLB of <linux/mman.h>");
static_assert(kMapHuge2Mb == MAP_HUGE_2MB, "kMapHuge2Mb is not MAP_HUGE_2MB of <linux/mman.h>");
static_assert(kAdviseRandom == MADV_RANDOM, "kAdviseRandom is not MADV_RANDOM of <linux/mman.h>");
static_assert(kAdviseSequential == MADV_SEQUENTIAL, "kAdviseSequential is not MADV_SEQUENTIAL of <linux/mman.h>");
static_assert(kAdviseWillNeed == MADV_WILLNEED, "kAdviseWillNeed is not MADV_WILLNEED of <linux/mman.h>");
static_assert(kAdviseDontNeed == MADV_DONTNEED, "kAdviseDontNeed is not MADV_DONTNEED of <linux/mman.h>");
static_assert(kAdviseFree == MADV_FREE, "kAdviseFree is not MADV_FREE of <linux/mman.h>");
static_assert(kAdviseRemove == MADV_REMOVE, "kAdviseRemove is not MADV_REMOVE of <linux/mman.h>");
static_assert(kAdviseHugePage == MADV_HUGEPAGE, "kAdviseHugePage is not MADV_HUGEPAGE of <linux/mman.h>");
static_assert(kAdviseNoHugePage == MADV_NOHUGEPAGE, "kAdviseNoHugePage is not MADV_NOHUGEPAGE of <linux/mman.h>");
static_assert(kAdviseDontDump == MADV_DONTDUMP, "kAdviseDontDump is not MADV_DONTDUMP of <linux/mman.h>");
static_assert(kAdviseWipeOnFork == MADV_WIPEONFORK, "kAdviseWipeOnFork is not MADV_WIPEONFORK of <linux/mman.h>");
static_assert(kAdviseCollapse == MADV_COLLAPSE, "kAdviseCollapse is not MADV_COLLAPSE of <linux/mman.h>");
static_assert(kLockOnFault == MLOCK_ONFAULT, "kLockOnFault is not MLOCK_ONFAULT of <linux/mman.h>");

// The argument words of foundation/os/MemoryPolicyWords.h are the words of the
// kernel header.
static_assert(kMemoryPolicyBind == MPOL_BIND, "kMemoryPolicyBind is not MPOL_BIND of <linux/mempolicy.h>");
static_assert(kMemoryPolicyStrict == MPOL_MF_STRICT,
              "kMemoryPolicyStrict is not MPOL_MF_STRICT of <linux/mempolicy.h>");
static_assert(kMemoryPolicyMove == MPOL_MF_MOVE, "kMemoryPolicyMove is not MPOL_MF_MOVE of <linux/mempolicy.h>");

// The argument words of foundation/os/FutexWords.h are the words of the
// kernel headers.
static_assert(kFutexWaitBitsetPrivate == FUTEX_WAIT_BITSET_PRIVATE,
              "kFutexWaitBitsetPrivate is not FUTEX_WAIT_BITSET_PRIVATE of <linux/futex.h>");
static_assert(kFutexWakePrivate == FUTEX_WAKE_PRIVATE,
              "kFutexWakePrivate is not FUTEX_WAKE_PRIVATE of <linux/futex.h>");
static_assert(kFutexBitsetMatchAny == FUTEX_BITSET_MATCH_ANY,
              "kFutexBitsetMatchAny is not FUTEX_BITSET_MATCH_ANY of <linux/futex.h>");
static_assert(sizeof(KernelTimespec) == sizeof(::__kernel_timespec),
              "KernelTimespec does not have the size of struct __kernel_timespec of <linux/time_types.h>");
static_assert(offsetof(KernelTimespec, seconds) == offsetof(::__kernel_timespec, tv_sec),
              "KernelTimespec::seconds is not at the place of tv_sec of struct __kernel_timespec");
static_assert(offsetof(KernelTimespec, nanoseconds) == offsetof(::__kernel_timespec, tv_nsec),
              "KernelTimespec::nanoseconds is not at the place of tv_nsec of struct __kernel_timespec");

// The argument words of foundation/os/SchedWords.h are the words of the kernel
// headers.
static_assert(kSchedNormal == SCHED_NORMAL, "kSchedNormal is not SCHED_NORMAL of <linux/sched.h>");
static_assert(kSchedFifo == SCHED_FIFO, "kSchedFifo is not SCHED_FIFO of <linux/sched.h>");
static_assert(kSchedRoundRobin == SCHED_RR, "kSchedRoundRobin is not SCHED_RR of <linux/sched.h>");
static_assert(kSchedBatch == SCHED_BATCH, "kSchedBatch is not SCHED_BATCH of <linux/sched.h>");
static_assert(kSchedIdle == SCHED_IDLE, "kSchedIdle is not SCHED_IDLE of <linux/sched.h>");
static_assert(kSchedDeadline == SCHED_DEADLINE, "kSchedDeadline is not SCHED_DEADLINE of <linux/sched.h>");
static_assert(kPriorityOfThread == PRIO_PROCESS, "kPriorityOfThread is not PRIO_PROCESS of <linux/resource.h>");
static_assert(kSchedAttrSize == SCHED_ATTR_SIZE_VER0,
              "kSchedAttrSize is not SCHED_ATTR_SIZE_VER0 of <linux/sched/types.h>");
static_assert(sizeof(KernelSchedAttr) == SCHED_ATTR_SIZE_VER0,
              "KernelSchedAttr does not have the size of the first published struct sched_attr");
static_assert(offsetof(KernelSchedAttr, size) == offsetof(::sched_attr, size),
              "KernelSchedAttr::size is not at the place of size of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, policy) == offsetof(::sched_attr, sched_policy),
              "KernelSchedAttr::policy is not at the place of sched_policy of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, flags) == offsetof(::sched_attr, sched_flags),
              "KernelSchedAttr::flags is not at the place of sched_flags of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, nice) == offsetof(::sched_attr, sched_nice),
              "KernelSchedAttr::nice is not at the place of sched_nice of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, priority) == offsetof(::sched_attr, sched_priority),
              "KernelSchedAttr::priority is not at the place of sched_priority of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, runtime_ns) == offsetof(::sched_attr, sched_runtime),
              "KernelSchedAttr::runtime_ns is not at the place of sched_runtime of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, deadline_ns) == offsetof(::sched_attr, sched_deadline),
              "KernelSchedAttr::deadline_ns is not at the place of sched_deadline of struct sched_attr");
static_assert(offsetof(KernelSchedAttr, period_ns) == offsetof(::sched_attr, sched_period),
              "KernelSchedAttr::period_ns is not at the place of sched_period of struct sched_attr");

namespace {

// The number of each call on the architecture of the build, from
// <asm/unistd.h>.  A value of the enumeration that names no call gets a
// number that no call has, and the kernel answers ENOSYS for it.
constexpr long kNoCall = -1;

[[nodiscard]] constexpr long number_of(Syscall call) noexcept {
    switch (call) {
        case Syscall::read:
            return __NR_read;
        case Syscall::write:
            return __NR_write;
        case Syscall::openat:
            return __NR_openat;
        case Syscall::close:
            return __NR_close;
        case Syscall::uname:
            return __NR_uname;
        case Syscall::getdents64:
            return __NR_getdents64;
        case Syscall::sched_getaffinity:
            return __NR_sched_getaffinity;
        case Syscall::futex:
            return __NR_futex;
        case Syscall::mmap:
            return __NR_mmap;
        case Syscall::mprotect:
            return __NR_mprotect;
        case Syscall::munmap:
            return __NR_munmap;
        case Syscall::fsync:
            return __NR_fsync;
        case Syscall::fdatasync:
            return __NR_fdatasync;
        case Syscall::fstat:
            return __NR_fstat;
        case Syscall::renameat:
            return __NR_renameat;
        case Syscall::renameat2:
            return __NR_renameat2;
        case Syscall::unlinkat:
            return __NR_unlinkat;
        case Syscall::mkdirat:
            return __NR_mkdirat;
        case Syscall::newfstatat:
            return __NR_newfstatat;
        case Syscall::madvise:
            return __NR_madvise;
        case Syscall::sendfile:
            return __NR_sendfile;
        case Syscall::copy_file_range:
            return __NR_copy_file_range;
        case Syscall::io_uring_setup:
            return __NR_io_uring_setup;
        case Syscall::ioctl:
            return __NR_ioctl;
        case Syscall::sched_setaffinity:
            return __NR_sched_setaffinity;
        case Syscall::sched_setscheduler:
            return __NR_sched_setscheduler;
        case Syscall::sched_setattr:
            return __NR_sched_setattr;
        case Syscall::setpriority:
            return __NR_setpriority;
        case Syscall::mbind:
            return __NR_mbind;
        case Syscall::move_pages:
            return __NR_move_pages;
        case Syscall::getpid:
            return __NR_getpid;
        case Syscall::setsockopt:
            return __NR_setsockopt;
        case Syscall::getsockopt:
            return __NR_getsockopt;
        case Syscall::bind:
            return __NR_bind;
        case Syscall::sendto:
            return __NR_sendto;
        case Syscall::recvfrom:
            return __NR_recvfrom;
        case Syscall::recvmsg:
            return __NR_recvmsg;
        case Syscall::sched_getattr:
            return __NR_sched_getattr;
        case Syscall::mlock:
            return __NR_mlock;
        case Syscall::mlock2:
            return __NR_mlock2;
        case Syscall::munlock:
            return __NR_munlock;
        case Syscall::prctl:
            return __NR_prctl;
        case Syscall::gettid:
            return __NR_gettid;
        case Syscall::perf_event_open:
            return __NR_perf_event_open;
        default:
            return kNoCall;
    }
}

// Each enumerator has its number, so no call of the enumeration reaches the
// kernel as a number that no call has.  The walk reads each enumerator, so a
// new enumerator with no case stops the build here.
[[nodiscard]] consteval bool each_call_has_a_number() noexcept {
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^Syscall)) {
        if (number_of(std::meta::extract<Syscall>(enumerator)) == kNoCall) {
            return false;
        }
    }
    return true;
}

static_assert(each_call_has_a_number(),
              "an enumerator of foundation::os::Syscall has no case in number_of: give it the __NR_ number of "
              "<asm/unistd.h>");

// Each call that can block has a route of a build with ThreadSanitizer: read
// and write go through the C library, and futex goes between the hooks.  The
// walk reads each enumerator, so a new blocking call with no route stops the
// build of each preset, and not only of the TSan preset.
[[nodiscard]] consteval bool each_blocking_call_has_a_route() noexcept {
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^Syscall)) {
        Syscall const call = std::meta::extract<Syscall>(enumerator);
        if (can_block(call) && call != Syscall::read && call != Syscall::write && call != Syscall::futex) {
            return false;
        }
    }
    return true;
}

static_assert(each_blocking_call_has_a_route(),
              "a call of foundation::os::Syscall can block and has no route of a build with ThreadSanitizer: give it "
              "a route in raw_syscall and in src/foundation/os/Intercepted.cpp");

// The one system call instruction.  The kernel reads the number and the six
// words from fixed registers, and it gives the result in one register.  Each
// register variable names the register of its operand, and no call stands
// between the stores into the variables and the instruction.
#if defined(__x86_64__)
// The instruction syscall changes rcx and r11, and the kernel can read and
// write each argument address.
[[gnu::always_inline]] inline long issue(long number, std::uint64_t first, std::uint64_t second, std::uint64_t third,
                                         std::uint64_t fourth, std::uint64_t fifth, std::uint64_t sixth) noexcept {
    register long result asm("rax") = number;
    register std::uint64_t word0 asm("rdi") = first;
    register std::uint64_t word1 asm("rsi") = second;
    register std::uint64_t word2 asm("rdx") = third;
    register std::uint64_t word3 asm("r10") = fourth;
    register std::uint64_t word4 asm("r8") = fifth;
    register std::uint64_t word5 asm("r9") = sixth;
    asm volatile("syscall"
                 : "+r"(result)
                 : "r"(word0), "r"(word1), "r"(word2), "r"(word3), "r"(word4), "r"(word5)
                 : "rcx", "r11", "memory");
    return result;
}
#elif defined(__aarch64__)
// The instruction svc #0 takes the number in x8 and the words in x0 to x5,
// and gives the result in x0.
[[gnu::always_inline]] inline long issue(long number, std::uint64_t first, std::uint64_t second, std::uint64_t third,
                                         std::uint64_t fourth, std::uint64_t fifth, std::uint64_t sixth) noexcept {
    register long call_number asm("x8") = number;
    register std::uint64_t word0 asm("x0") = first;
    register std::uint64_t word1 asm("x1") = second;
    register std::uint64_t word2 asm("x2") = third;
    register std::uint64_t word3 asm("x3") = fourth;
    register std::uint64_t word4 asm("x4") = fifth;
    register std::uint64_t word5 asm("x5") = sixth;
    asm volatile("svc #0"
                 : "+r"(word0)
                 : "r"(call_number), "r"(word1), "r"(word2), "r"(word3), "r"(word4), "r"(word5)
                 : "memory", "cc");
    return static_cast<long>(word0);
}
#else
#error "the raw system call primitive supports x86_64 and aarch64 only.  Add the instruction of the new architecture."
#endif

#if defined(__SANITIZE_THREAD__)
// The futex route of a build with ThreadSanitizer (THE SANITIZERS of
// foundation/os/Syscall.h).  The command of the operation word, without the
// two flags, tells what the call does for the sanitizer.
[[nodiscard]] detail::FutexRole futex_role(std::uint64_t operation) noexcept {
    auto const command = static_cast<std::uint32_t>(operation) & static_cast<std::uint32_t>(FUTEX_CMD_MASK);
    if (command == FUTEX_WAKE || command == FUTEX_WAKE_BITSET) {
        return detail::FutexRole::wake;
    }
    if (command == FUTEX_WAIT || command == FUTEX_WAIT_BITSET) {
        return detail::FutexRole::wait;
    }
    return detail::FutexRole::other;
}

// The one raw futex call, between the two halves of the route of
// src/foundation/os/Intercepted.cpp.
long issue_watched_futex(detail::FutexCall const& call) noexcept {
    detail::enter_futex(call);
    long const result = issue(__NR_futex, call.address, call.operation, call.value, call.timeout, call.second_address,
                              call.third_value);
    detail::leave_futex(call, result);
    return result;
}
#endif

// THE KERNEL FLOOR.  The first release of each architecture: x86_64 has each
// call of the 2.6.0 table at 2.6.0, and aarch64 came in 3.7.0 with each call
// of the generic table.
#if defined(__x86_64__)
constexpr KernelVersion kFirstRelease{2, 6, 0};
#elif defined(__aarch64__)
constexpr KernelVersion kFirstRelease{3, 7, 0};
#endif

// The release in which a call came to the architecture of the build, with the
// operations that the doors give it: the later of the first release of the
// architecture and the release that added the call.  openat, renameat,
// unlinkat, mkdirat and newfstatat came in 2.6.16.  mbind came in 2.6.7, and
// the memory policy door uses
// MPOL_MF_MOVE, which came in 2.6.16.  move_pages came in 2.6.18.  futex came
// in 2.6.0, and the futex door uses FUTEX_WAIT_BITSET, which came in 2.6.25.
// perf_event_open came in 2.6.31, sched_setattr and sched_getattr in 3.14,
// renameat2 in 3.15, mlock2 in 4.4, copy_file_range in 4.5 and io_uring_setup
// in 5.1.  gettid came before 2.6.0.
[[nodiscard]] constexpr KernelVersion introduced_in(Syscall call) noexcept {
    KernelVersion added = kFirstRelease;
    switch (call) {
        case Syscall::openat:
        case Syscall::renameat:
        case Syscall::unlinkat:
        case Syscall::mkdirat:
        case Syscall::newfstatat:
        case Syscall::mbind:
            added = KernelVersion{2, 6, 16};
            break;
        case Syscall::move_pages:
            added = KernelVersion{2, 6, 18};
            break;
        case Syscall::futex:
            added = KernelVersion{2, 6, 25};
            break;
        case Syscall::perf_event_open:
            added = KernelVersion{2, 6, 31};
            break;
        case Syscall::sched_setattr:
        case Syscall::sched_getattr:
            added = KernelVersion{3, 14, 0};
            break;
        case Syscall::renameat2:
            added = KernelVersion{3, 15, 0};
            break;
        case Syscall::mlock2:
            added = KernelVersion{4, 4, 0};
            break;
        case Syscall::copy_file_range:
            added = KernelVersion{4, 5, 0};
            break;
        case Syscall::io_uring_setup:
            added = KernelVersion{5, 1, 0};
            break;
        case Syscall::read:
        case Syscall::write:
        case Syscall::close:
        case Syscall::uname:
        case Syscall::getdents64:
        case Syscall::sched_getaffinity:
        case Syscall::mmap:
        case Syscall::mprotect:
        case Syscall::munmap:
        case Syscall::fsync:
        case Syscall::fdatasync:
        case Syscall::fstat:
        case Syscall::madvise:
        case Syscall::sendfile:
        case Syscall::ioctl:
        case Syscall::sched_setaffinity:
        case Syscall::sched_setscheduler:
        case Syscall::setpriority:
        case Syscall::getpid:
        case Syscall::setsockopt:
        case Syscall::getsockopt:
        case Syscall::bind:
        case Syscall::sendto:
        case Syscall::recvfrom:
        case Syscall::recvmsg:
        case Syscall::mlock:
        case Syscall::munlock:
        case Syscall::prctl:
        case Syscall::gettid:
        default:
            break;
    }
    return added > kFirstRelease ? added : kFirstRelease;
}

// True when the call is a feature that the caller of a door chooses (THE
// FEATURE CALLS of foundation/os/KernelFloor.h): renameat2 gives the commit
// that refuses an existing target, copy_file_range the copy between two
// files, io_uring_setup the ring of io_uring, sched_setattr and sched_getattr
// the scheduling attributes, mlock2 the lock of a page when it faults in, and
// perf_event_open a perf event.  The door gives the error enosys of an older
// kernel to its caller.
[[nodiscard]] constexpr bool is_feature_call(Syscall call) noexcept {
    return call == Syscall::renameat2 || call == Syscall::copy_file_range || call == Syscall::io_uring_setup
        || call == Syscall::sched_setattr || call == Syscall::sched_getattr || call == Syscall::mlock2
        || call == Syscall::perf_event_open;
}

// The largest release that an enumerator of Syscall needs, other than a
// feature call.  The walk reads each enumerator, so a new call joins the
// derivation with no other edit.
[[nodiscard]] consteval KernelVersion derive_minimum_kernel() noexcept {
    KernelVersion largest = kFirstRelease;
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^Syscall)) {
        Syscall const call = std::meta::extract<Syscall>(enumerator);
        if (is_feature_call(call)) {
            continue;
        }
        KernelVersion const needed = introduced_in(call);
        if (needed > largest) {
            largest = needed;
        }
    }
    return largest;
}

constexpr KernelVersion kMinimumKernel = derive_minimum_kernel();

// The count of the characters of the release field of uname, without its
// zero.
constexpr std::size_t kReleaseCharacters = __NEW_UTS_LEN;

// One part of a release text: the digits at AT, which must hold at least one
// digit and fit in 16 bits.  AT moves past the digits that it read.
[[nodiscard]] bool read_part(char const* release, std::size_t& at, std::uint16_t& part) noexcept {
    std::uint32_t value = 0;
    std::size_t const first = at;
    while (at < kReleaseCharacters && release[at] >= '0' && release[at] <= '9') {
        value = value * 10U + static_cast<std::uint32_t>(release[at] - '0');
        if (value > 0xFFFFU) {
            return false;
        }
        ++at;
    }
    part = static_cast<std::uint16_t>(value);
    return at > first;
}

// Appends TEXT up to its zero to BYTES from AT, and stops at CAPACITY.
void append_text(char* bytes, std::size_t capacity, std::size_t& at, char const* text,
                 std::size_t text_bound) noexcept {
    for (std::size_t index = 0; index < text_bound && text[index] != '\0' && at < capacity; ++index) {
        bytes[at] = text[index];
        ++at;
    }
}

void append_number(char* bytes, std::size_t capacity, std::size_t& at, std::uint16_t number) noexcept {
    char digits[5] = {};
    std::size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + number % 10U);
        ++count;
        number = static_cast<std::uint16_t>(number / 10U);
    } while (number != 0U);
    while (count > 0 && at < capacity) {
        --count;
        bytes[at] = digits[count];
        ++at;
    }
}

// Appends the text of a release to BYTES from AT: MAJOR.MINOR.PATCH.
void append_release(char* bytes, std::size_t capacity, std::size_t& at, KernelVersion release) noexcept {
    append_number(bytes, capacity, at, release.major);
    append_text(bytes, capacity, at, ".", 1);
    append_number(bytes, capacity, at, release.minor);
    append_text(bytes, capacity, at, ".", 1);
    append_number(bytes, capacity, at, release.patch);
}

// Writes the bytes to standard error, again after a short write and after a
// signal.  The process ends after the write, so a write that fails has no
// caller to tell, and the function stops.
void write_to_standard_error(char const* bytes, std::size_t count) noexcept {
    constexpr int kStandardError = 2;
    std::size_t written = 0;
    while (written < count) {
        long const taken = raw_syscall(Syscall::write, kStandardError, bytes + written, count - written);
        if (taken > 0) {
            written += static_cast<std::size_t>(taken);
        } else if (taken != -EINTR) {
            return;
        }
    }
}

// The start-up check (foundation/os/KernelFloor.h).  The priority 101 is the
// first priority of a program, so the check runs before each constructor of
// the program that has no priority.  The check reads the release one time,
// and it holds no state.
[[gnu::constructor(101)]] void refuse_an_older_kernel() noexcept {
    ::new_utsname names{};
    long const word = raw_syscall(Syscall::uname, static_cast<void const*>(&names));
    KernelVerdict const verdict = word < 0 ? KernelVerdict::unreadable : judge_release(names.release);
    if (verdict == KernelVerdict::admitted) {
        return;
    }
    char message[256] = {};
    std::size_t at = 0;
    append_text(message, sizeof message, at, "foundation: the kernel release \"", 64);
    append_text(message, sizeof message, at, names.release, kReleaseCharacters);
    append_text(message, sizeof message, at,
                verdict == KernelVerdict::too_old ? "\" is older than "
                                                  : "\" is no MAJOR.MINOR release, and the base needs ",
                64);
    append_release(message, sizeof message, at, kMinimumKernel);
    append_text(message, sizeof message, at, ", the release that the raw system calls of the base need\n", 96);
    write_to_standard_error(message, at);
    __builtin_trap();
}

}  // namespace

long raw_syscall(Syscall call, Word first, Word second, Word third, Word fourth, Word fifth, Word sixth) noexcept {
#if defined(__SANITIZE_THREAD__)
    if (call == Syscall::futex) {
        return issue_watched_futex(detail::FutexCall{futex_role(second.bits()), first.bits(), second.bits(),
                                                     third.bits(), fourth.bits(), fifth.bits(), sixth.bits()});
    }
    if (call == Syscall::read) {
        return detail::read_intercepted(first.bits(), second.bits(), third.bits());
    }
    if (call == Syscall::write) {
        return detail::write_intercepted(first.bits(), second.bits(), third.bits());
    }
#endif
    return issue(number_of(call), first.bits(), second.bits(), third.bits(), fourth.bits(), fifth.bits(), sixth.bits());
}

KernelVersion minimum_kernel() noexcept { return kMinimumKernel; }

KernelVerdict judge_release(char const* release) noexcept {
    if (release == nullptr) {
        return KernelVerdict::unreadable;
    }
    KernelVersion running{};
    std::size_t at = 0;
    if (!read_part(release, at, running.major) || at >= kReleaseCharacters || release[at] != '.') {
        return KernelVerdict::unreadable;
    }
    ++at;
    if (!read_part(release, at, running.minor)) {
        return KernelVerdict::unreadable;
    }
    if (at < kReleaseCharacters && release[at] == '.') {
        ++at;
        std::size_t const before_patch = at;
        if (!read_part(release, at, running.patch) && at != before_patch) {
            return KernelVerdict::unreadable;
        }
    }
    return running < kMinimumKernel ? KernelVerdict::too_old : KernelVerdict::admitted;
}

}  // namespace foundation::os
