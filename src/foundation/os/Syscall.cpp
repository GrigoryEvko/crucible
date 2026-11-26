// The door of the raw system call primitive (foundation/os/Syscall.h), of
// the kernel floor (foundation/os/KernelFloor.h) and of the block of SIGPIPE
// (foundation/os/PipeSignal.h).
//
// This file is the one place of the base with the system call instruction,
// and the one includer of <asm/unistd.h>, <asm-generic/errno-base.h>,
// <linux/fcntl.h>, <linux/futex.h>, <linux/memfd.h>, <linux/mempolicy.h>,
// <linux/mman.h>, <linux/resource.h>, <linux/sched.h>, <linux/sched/types.h>,
// <linux/time_types.h> and <linux/utsname.h>.  It includes no header of the
// C library, because some declarations of the two kinds of header conflict.
// In a build with ThreadSanitizer, the primitive gives a read or a write that
// can block, a map and an unmap to src/foundation/os/Intercepted.cpp, which is
// the one door of the floor that calls the C library.  It issues a futex
// call, a clock_nanosleep, a close, an openat and an openat2 raw, between the
// hooks of that file.

#include <foundation/os/FutexWords.h>
#include <foundation/os/KernelFloor.h>
#include <foundation/os/MapWords.h>
#include <foundation/os/MemoryPolicyWords.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/PipeSignal.h>
#include <foundation/os/SchedWords.h>
#include <foundation/os/Syscall.h>
#include <foundation/reflect/EnumName.h>

#include <asm-generic/errno-base.h>
#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
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

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#include "Intercepted.h"
#endif

namespace foundation::os {

// The argument words of foundation/os/OpenFlags.h are the words of the kernel
// header.
static_assert(kAtCurrentDirectory == AT_FDCWD, "kAtCurrentDirectory is not AT_FDCWD of <linux/fcntl.h>");
static_assert(kAtSymlinkNoFollow == AT_SYMLINK_NOFOLLOW,
              "kAtSymlinkNoFollow is not AT_SYMLINK_NOFOLLOW of <linux/fcntl.h>");
static_assert(kAtRemoveDirectory == AT_REMOVEDIR, "kAtRemoveDirectory is not AT_REMOVEDIR of <linux/fcntl.h>");
static_assert(kAtEmptyPath == AT_EMPTY_PATH, "kAtEmptyPath is not AT_EMPTY_PATH of <linux/fcntl.h>");
static_assert(kAtStatusNoSync == AT_STATX_DONT_SYNC, "kAtStatusNoSync is not AT_STATX_DONT_SYNC of <linux/fcntl.h>");
static_assert(kOpenReadOnly == O_RDONLY, "kOpenReadOnly is not O_RDONLY of <linux/fcntl.h>");
static_assert(kOpenWriteOnly == O_WRONLY, "kOpenWriteOnly is not O_WRONLY of <linux/fcntl.h>");
static_assert(kOpenReadWrite == O_RDWR, "kOpenReadWrite is not O_RDWR of <linux/fcntl.h>");
static_assert(kOpenCreate == O_CREAT, "kOpenCreate is not O_CREAT of <linux/fcntl.h>");
static_assert(kOpenExclusive == O_EXCL, "kOpenExclusive is not O_EXCL of <linux/fcntl.h>");
static_assert(kOpenNoControllingTerminal == O_NOCTTY, "kOpenNoControllingTerminal is not O_NOCTTY of <linux/fcntl.h>");
static_assert(kOpenTruncate == O_TRUNC, "kOpenTruncate is not O_TRUNC of <linux/fcntl.h>");
static_assert(kOpenAppend == O_APPEND, "kOpenAppend is not O_APPEND of <linux/fcntl.h>");
static_assert(kOpenDataSync == O_DSYNC, "kOpenDataSync is not O_DSYNC of <linux/fcntl.h>");
static_assert(kOpenFullSync == O_SYNC, "kOpenFullSync is not O_SYNC of <linux/fcntl.h>");
static_assert(kOpenCloseOnExec == O_CLOEXEC, "kOpenCloseOnExec is not O_CLOEXEC of <linux/fcntl.h>");
static_assert(kOpenPath == O_PATH, "kOpenPath is not O_PATH of <linux/fcntl.h>");
static_assert(kOpenDirectory == O_DIRECTORY, "kOpenDirectory is not O_DIRECTORY of <linux/fcntl.h>");
static_assert(kOpenNoFollow == O_NOFOLLOW, "kOpenNoFollow is not O_NOFOLLOW of <linux/fcntl.h>");
static_assert(kOpenDirect == O_DIRECT, "kOpenDirect is not O_DIRECT of <linux/fcntl.h>");
static_assert(kOpenTemporary == O_TMPFILE, "kOpenTemporary is not O_TMPFILE of <linux/fcntl.h>");

// The argument and the resolve flags of openat2 are the words of
// <linux/openat2.h>, which <linux/fcntl.h> includes.
static_assert(kResolveNoMagicLinks == RESOLVE_NO_MAGICLINKS,
              "kResolveNoMagicLinks is not RESOLVE_NO_MAGICLINKS of <linux/openat2.h>");
static_assert(kResolveBeneath == RESOLVE_BENEATH, "kResolveBeneath is not RESOLVE_BENEATH of <linux/openat2.h>");
static_assert(sizeof(OpenHow) == sizeof(::open_how),
              "OpenHow does not have the size of struct open_how of <linux/openat2.h>");
static_assert(offsetof(OpenHow, flags) == offsetof(::open_how, flags),
              "OpenHow::flags is not at the place of flags of struct open_how");
static_assert(offsetof(OpenHow, mode) == offsetof(::open_how, mode),
              "OpenHow::mode is not at the place of mode of struct open_how");
static_assert(offsetof(OpenHow, resolve) == offsetof(::open_how, resolve),
              "OpenHow::resolve is not at the place of resolve of struct open_how");

// The flags of a memory file are the words of <linux/memfd.h>, and its seals
// the words of <linux/fcntl.h>.
static_assert(kMemoryFileCloseOnExec == MFD_CLOEXEC, "kMemoryFileCloseOnExec is not MFD_CLOEXEC of <linux/memfd.h>");
static_assert(kMemoryFileAllowSeals == MFD_ALLOW_SEALING,
              "kMemoryFileAllowSeals is not MFD_ALLOW_SEALING of <linux/memfd.h>");
static_assert(kMemoryFileNoExecSeal == MFD_NOEXEC_SEAL,
              "kMemoryFileNoExecSeal is not MFD_NOEXEC_SEAL of <linux/memfd.h>");
static_assert(kFcntlDuplicateCloseOnExec == F_DUPFD_CLOEXEC,
              "kFcntlDuplicateCloseOnExec is not F_DUPFD_CLOEXEC of <linux/fcntl.h>");
static_assert(kFcntlAddSeals == F_ADD_SEALS, "kFcntlAddSeals is not F_ADD_SEALS of <linux/fcntl.h>");
static_assert(kSealSeal == F_SEAL_SEAL, "kSealSeal is not F_SEAL_SEAL of <linux/fcntl.h>");
static_assert(kSealShrink == F_SEAL_SHRINK, "kSealShrink is not F_SEAL_SHRINK of <linux/fcntl.h>");

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
static_assert(kMapNoReserve == MAP_NORESERVE, "kMapNoReserve is not MAP_NORESERVE of <linux/mman.h>");
static_assert(kMapPopulate == MAP_POPULATE, "kMapPopulate is not MAP_POPULATE of <linux/mman.h>");
static_assert(kMapHugeTlb == MAP_HUGETLB, "kMapHugeTlb is not MAP_HUGETLB of <linux/mman.h>");
static_assert(kMapHuge2Mb == MAP_HUGE_2MB, "kMapHuge2Mb is not MAP_HUGE_2MB of <linux/mman.h>");
static_assert(kMapFixedNoReplace == MAP_FIXED_NOREPLACE,
              "kMapFixedNoReplace is not MAP_FIXED_NOREPLACE of <linux/mman.h>");
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
static_assert(kAdvisePopulateRead == MADV_POPULATE_READ,
              "kAdvisePopulateRead is not MADV_POPULATE_READ of <linux/mman.h>");
static_assert(kAdvisePopulateWrite == MADV_POPULATE_WRITE,
              "kAdvisePopulateWrite is not MADV_POPULATE_WRITE of <linux/mman.h>");
static_assert(kAdviseCollapse == MADV_COLLAPSE, "kAdviseCollapse is not MADV_COLLAPSE of <linux/mman.h>");
static_assert(kLockOnFault == MLOCK_ONFAULT, "kLockOnFault is not MLOCK_ONFAULT of <linux/mman.h>");
static_assert(kRemapMayMove == MREMAP_MAYMOVE, "kRemapMayMove is not MREMAP_MAYMOVE of <linux/mman.h>");
static_assert(kRemapFixed == MREMAP_FIXED, "kRemapFixed is not MREMAP_FIXED of <linux/mman.h>");

// The argument words of foundation/os/MemoryPolicyWords.h are the words of the
// kernel header.
static_assert(kMemoryPolicyDefault == MPOL_DEFAULT, "kMemoryPolicyDefault is not MPOL_DEFAULT of <linux/mempolicy.h>");
static_assert(kMemoryPolicyBind == MPOL_BIND, "kMemoryPolicyBind is not MPOL_BIND of <linux/mempolicy.h>");
static_assert(kMemoryPolicyPreferred == MPOL_PREFERRED,
              "kMemoryPolicyPreferred is not MPOL_PREFERRED of <linux/mempolicy.h>");
static_assert(kMemoryPolicyInterleave == MPOL_INTERLEAVE,
              "kMemoryPolicyInterleave is not MPOL_INTERLEAVE of <linux/mempolicy.h>");
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
        case Syscall::mincore:
            return __NR_mincore;
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
        case Syscall::get_mempolicy:
            return __NR_get_mempolicy;
        case Syscall::set_mempolicy:
            return __NR_set_mempolicy;
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
        case Syscall::openat2:
            return __NR_openat2;
        case Syscall::socket:
            return __NR_socket;
        case Syscall::pipe2:
            return __NR_pipe2;
        case Syscall::dup3:
            return __NR_dup3;
        case Syscall::clock_gettime:
            return __NR_clock_gettime;
        case Syscall::clock_nanosleep:
            return __NR_clock_nanosleep;
        case Syscall::ptrace:
            return __NR_ptrace;
        case Syscall::pread:
            return __NR_pread64;
        case Syscall::ftruncate:
            return __NR_ftruncate;
        case Syscall::mremap:
            return __NR_mremap;
        case Syscall::getcpu:
            return __NR_getcpu;
        case Syscall::pidfd_open:
            return __NR_pidfd_open;
        case Syscall::geteuid:
            return __NR_geteuid;
        case Syscall::memfd_create:
            return __NR_memfd_create;
        case Syscall::fallocate:
            return __NR_fallocate;
        case Syscall::fcntl:
            return __NR_fcntl;
        case Syscall::statx:
            return __NR_statx;
        case Syscall::socketpair:
            return __NR_socketpair;
        case Syscall::shutdown:
            return __NR_shutdown;
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
// and write go through the C library, and futex and clock_nanosleep go
// between the hooks.  The walk reads each enumerator, so a new blocking call
// with no route stops the build of each preset, and not only of the TSan
// preset.
[[nodiscard]] consteval bool has_a_route(Syscall call) noexcept {
    return call == Syscall::read || call == Syscall::write || call == Syscall::futex
        || call == Syscall::clock_nanosleep;
}

[[nodiscard]] consteval bool each_blocking_call_has_a_route() noexcept {
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^Syscall)) {
        Syscall const call = std::meta::extract<Syscall>(enumerator);
        if (can_block(call) && !has_a_route(call)) {
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
// The futex route of a build with ThreadSanitizer (the sanitizers of
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

// The one raw clock_nanosleep, between the two halves of the route of
// src/foundation/os/Intercepted.cpp.
long issue_watched_sleep(detail::SleepWords const& words) noexcept {
    detail::enter_sleep(words);
    long const result = issue(__NR_clock_nanosleep, words.clock, words.flags, words.request, words.remain, 0, 0);
    detail::leave_sleep(words, result);
    return result;
}

// The one raw close, between the two hooks of the sanitizer.
long issue_watched_close(std::uint64_t descriptor) noexcept {
    detail::enter_close(descriptor);
    long const result = issue(__NR_close, descriptor, 0, 0, 0, 0, 0);
    detail::leave_close(descriptor, result);
    return result;
}

// The one raw openat or openat2, between the two hooks of the sanitizer.
long issue_watched_open(detail::OpenWords const& words) noexcept {
    detail::enter_open(words);
    long const number = words.call == detail::OpenCall::openat ? __NR_openat : __NR_openat2;
    long const result = issue(number, words.directory, words.path, words.third, words.fourth, 0, 0);
    detail::leave_open(words, result);
    return result;
}
#endif

// Pipe signal (foundation/os/PipeSignal.h).  The signal set of the kernel
// holds 64 signals on x86_64 and on aarch64, with the bit N - 1 for the
// signal N.  So the set of the one signal SIGPIPE is one word, and the kernel
// refuses a set of a different size with EINVAL.  No enumerator of Syscall
// names rt_sigprocmask or rt_sigtimedwait, so this set is the one set of the
// two calls in the base.
//
// The words are the values of <asm/signal.h> and
// <asm-generic/signal-defs.h> on the two architectures: SIGPIPE is 13,
// SIG_BLOCK is 0 and SIG_UNBLOCK is 1.  This file cannot include
// <asm/signal.h>, because its sigset_t conflicts with the sigset_t of the C
// library, which <meta> brings in.  test_report_sink_pipe reads the mask
// through the C library, so a wrong word fails the test.
constexpr std::uint64_t kPipeSignal = 13;
constexpr std::uint64_t kPipeSignalSet = std::uint64_t{1} << (kPipeSignal - 1);
constexpr std::uint64_t kKernelSignalSetBytes = sizeof(std::uint64_t);
constexpr std::uint64_t kSignalBlock = 0;
constexpr std::uint64_t kSignalUnblock = 1;

// The word of an address that the kernel reads or writes.
[[nodiscard]] std::uint64_t word_of(void const* address) noexcept { return __builtin_bit_cast(std::uint64_t, address); }

// The words of each call are valid, so the kernel refuses rt_sigprocmask
// only for a set size that is not its own.  That is a defect of this file,
// which test_report_sink_pipe shows, and the process ends.  The pair cannot
// report the defect, because the report sink writes under a block.
void refuse_a_failed_mask_change(long word) noexcept {
    if (word != 0) {
        __builtin_trap();
    }
}

// Kernel floor.  The first release of each architecture: x86_64 has each
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
// unlinkat, mkdirat and newfstatat came in 2.6.16.  mbind, get_mempolicy and
// set_mempolicy came in 2.6.7, and the memory policy door uses
// MPOL_MF_MOVE, which came in 2.6.16.  move_pages came in 2.6.18, and pipe2
// and dup3 came in 2.6.27.  socketpair came before 2.6.0, and the pair door
// gives it SOCK_CLOEXEC in its type, which came in 2.6.27.  futex came
// in 2.6.0, and the futex door uses FUTEX_WAIT_BITSET, which came in 2.6.25.
// perf_event_open came in 2.6.31, sched_setattr and sched_getattr in 3.14,
// renameat2 in 3.15, mlock2 in 4.4, copy_file_range in 4.5, io_uring_setup in
// 5.1 and openat2 in 5.6.  ptrace came before 2.6.0, and the trace door of
// fixy/os/Process.h reads each stop with PTRACE_GET_SYSCALL_INFO, which came in
// 5.3.  pidfd_open came in 5.3.  mincore, gettid, geteuid, clock_gettime,
// clock_nanosleep and mremap came before 2.6.0, and getcpu came in 2.6.19.
// fallocate came in 2.6.23 and memfd_create in 3.17.  fcntl came before 2.6.0,
// and the file door uses its F_ADD_SEALS, which came in 3.17.  statx came in
// 4.11.
// The file door opens
// each path under a directory with openat2 and RESOLVE_BENEATH, and an open
// with openat would follow a path out of the directory with no error.  So
// openat2 is no feature call, and it sets the floor at 5.6.
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
        case Syscall::get_mempolicy:
        case Syscall::set_mempolicy:
            added = KernelVersion{2, 6, 7};
            break;
        case Syscall::move_pages:
            added = KernelVersion{2, 6, 18};
            break;
        case Syscall::getcpu:
            added = KernelVersion{2, 6, 19};
            break;
        case Syscall::fallocate:
            added = KernelVersion{2, 6, 23};
            break;
        case Syscall::pipe2:
        case Syscall::dup3:
        case Syscall::socketpair:
            added = KernelVersion{2, 6, 27};
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
        case Syscall::memfd_create:
        case Syscall::fcntl:
            added = KernelVersion{3, 17, 0};
            break;
        case Syscall::mlock2:
            added = KernelVersion{4, 4, 0};
            break;
        case Syscall::copy_file_range:
            added = KernelVersion{4, 5, 0};
            break;
        case Syscall::statx:
            added = KernelVersion{4, 11, 0};
            break;
        case Syscall::io_uring_setup:
            added = KernelVersion{5, 1, 0};
            break;
        case Syscall::openat2:
            added = KernelVersion{5, 6, 0};
            break;
        case Syscall::ptrace:
        case Syscall::pidfd_open:
            added = KernelVersion{5, 3, 0};
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
        case Syscall::mincore:
        case Syscall::sendfile:
        case Syscall::ioctl:
        case Syscall::sched_setaffinity:
        case Syscall::sched_setscheduler:
        case Syscall::setpriority:
        case Syscall::getpid:
        case Syscall::setsockopt:
        case Syscall::getsockopt:
        case Syscall::socket:
        case Syscall::shutdown:
        case Syscall::bind:
        case Syscall::sendto:
        case Syscall::recvfrom:
        case Syscall::recvmsg:
        case Syscall::mlock:
        case Syscall::munlock:
        case Syscall::prctl:
        case Syscall::gettid:
        case Syscall::geteuid:
        case Syscall::clock_gettime:
        case Syscall::clock_nanosleep:
        case Syscall::pread:
        case Syscall::ftruncate:
        case Syscall::mremap:
        default:
            break;
    }
    return added > kFirstRelease ? added : kFirstRelease;
}

// True when the call is a feature that the caller of a door chooses (the
// feature calls of foundation/os/KernelFloor.h): renameat2 gives the commit
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
// feature call, and that a flag of the need each_call needs (the flags of
// foundation/os/KernelFloor.h).  The walks read each enumerator, so a new
// call or a new flag joins the derivation with no other edit.
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
    ::foundation::reflect::for_each_enumerator_value<KernelFlag>([&largest](KernelFlag flag) {
        FlagFloor const row = flag_floor(flag);
        if (row.need == FlagNeed::each_call && row.release > largest) {
            largest = row.release;
        }
    });
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

// Reads the release text RELEASE into RUNNING (judge_release of
// foundation/os/KernelFloor.h).  False for a text that does not start with
// MAJOR.MINOR, and for a part that does not fit in 16 bits.
[[nodiscard]] bool read_release(char const* release, KernelVersion& running) noexcept {
    if (release == nullptr) {
        return false;
    }
    std::size_t at = 0;
    if (!read_part(release, at, running.major) || at >= kReleaseCharacters || release[at] != '.') {
        return false;
    }
    ++at;
    if (!read_part(release, at, running.minor)) {
        return false;
    }
    if (at < kReleaseCharacters && release[at] == '.') {
        ++at;
        std::size_t const before_patch = at;
        if (!read_part(release, at, running.patch) && at != before_patch) {
            return false;
        }
    }
    return true;
}

// The release of the running kernel that the start-up check read.  The check
// writes it before main and before each other constructor, and no code writes
// it after that, so a read needs no atomic.  Each program and each shared
// library that links the primitive has its own copy.
constinit KernelVersion running_at_start{};

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
        // The second test states the bound of count to the compiler.
    } while (number != 0U && count < sizeof digits);
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
// caller to tell, and the function stops.  A standard error with no reader
// gives epipe and no SIGPIPE (the pipe signal of foundation/os/PipeSignal.h),
// so the process ends by the trap of the check.
void write_to_standard_error(char const* bytes, std::size_t count) noexcept {
    constexpr int kStandardError = 2;
    PipeSignalBlock block = PipeSignalBlock::block_calling_thread();
    std::size_t written = 0;
    while (written < count) {
        long const taken = raw_syscall(Syscall::write, kStandardError, bytes + written, count - written);
        if (taken > 0) {
            written += static_cast<std::size_t>(taken);
            continue;
        }
        if (taken == -EPIPE) {
            block.mark_broken_pipe();
        }
        if (taken != -EINTR) {
            return;
        }
    }
}

// The start-up check (foundation/os/KernelFloor.h).  The priority 101 is the
// first priority of a program, so the check runs before each constructor of
// the program that has no priority.  The check reads the release one time,
// and it keeps the release of an admitted kernel for running_kernel().
[[gnu::constructor(101)]] void refuse_an_older_kernel() noexcept {
    ::new_utsname names{};
    long const word = raw_syscall(Syscall::uname, static_cast<void const*>(&names));
    KernelVersion running{};
    bool const is_readable = word >= 0 && read_release(names.release, running);
    KernelVerdict const verdict = !is_readable             ? KernelVerdict::unreadable
                                : running < kMinimumKernel ? KernelVerdict::too_old
                                                           : KernelVerdict::admitted;
    if (verdict == KernelVerdict::admitted) {
        running_at_start = running;
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

namespace detail {

bool block_pipe_signal_() noexcept {
    std::uint64_t const pipe = kPipeSignalSet;
    std::uint64_t previous = 0;
    refuse_a_failed_mask_change(
        issue(__NR_rt_sigprocmask, kSignalBlock, word_of(&pipe), word_of(&previous), kKernelSignalSetBytes, 0, 0));
    return (previous & kPipeSignalSet) != 0;
}

void release_pipe_signal_(bool was_blocked, bool has_broken_pipe) noexcept {
    if (was_blocked) {
        return;
    }
    std::uint64_t const pipe = kPipeSignalSet;
    if (has_broken_pipe) {
        // A wait of zero takes the pending signal, or gives EAGAIN when no
        // signal is pending.  The kernel does not sleep for a wait of zero,
        // so no other signal interrupts it with EINTR.
        KernelTimespec const no_wait{};
        static_cast<void>(
            issue(__NR_rt_sigtimedwait, word_of(&pipe), 0, word_of(&no_wait), kKernelSignalSetBytes, 0, 0));
    }
    refuse_a_failed_mask_change(
        issue(__NR_rt_sigprocmask, kSignalUnblock, word_of(&pipe), 0, kKernelSignalSetBytes, 0, 0));
}

}  // namespace detail

long raw_syscall(Syscall call, Word first, Word second, Word third, Word fourth, Word fifth, Word sixth) noexcept {
#if defined(__SANITIZE_THREAD__)
    if (call == Syscall::futex) {
        return issue_watched_futex(detail::FutexCall{futex_role(second.bits()), first.bits(), second.bits(),
                                                     third.bits(), fourth.bits(), fifth.bits(), sixth.bits()});
    }
    if (call == Syscall::clock_nanosleep) {
        return issue_watched_sleep(detail::SleepWords{first.bits(), second.bits(), third.bits(), fourth.bits()});
    }
    if (call == Syscall::read) {
        return detail::read_intercepted(first.bits(), second.bits(), third.bits());
    }
    if (call == Syscall::write) {
        return detail::write_intercepted(first.bits(), second.bits(), third.bits());
    }
    if (call == Syscall::close) {
        return issue_watched_close(first.bits());
    }
    if (call == Syscall::openat || call == Syscall::openat2) {
        detail::OpenCall const open_call =
            call == Syscall::openat ? detail::OpenCall::openat : detail::OpenCall::openat2;
        return issue_watched_open(
            detail::OpenWords{open_call, first.bits(), second.bits(), third.bits(), fourth.bits()});
    }
#endif
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    // Only the interceptors of mmap and munmap reset the shadow of a range.
    // The interceptor of AddressSanitizer resets a large range with a new map
    // of its shadow pages, and it touches no page (the sanitizers of
    // foundation/os/Syscall.h).
    if (call == Syscall::mmap) {
        return detail::map_intercepted(first.bits(), second.bits(), third.bits(), fourth.bits(), fifth.bits(),
                                       sixth.bits());
    }
    if (call == Syscall::munmap) {
        return detail::unmap_intercepted(first.bits(), second.bits());
    }
#endif
    return issue(number_of(call), first.bits(), second.bits(), third.bits(), fourth.bits(), fifth.bits(), sixth.bits());
}

KernelVersion minimum_kernel() noexcept { return kMinimumKernel; }

KernelVersion running_kernel() noexcept { return running_at_start; }

KernelVerdict judge_release(char const* release) noexcept {
    KernelVersion running{};
    if (!read_release(release, running)) {
        return KernelVerdict::unreadable;
    }
    return running < kMinimumKernel ? KernelVerdict::too_old : KernelVerdict::admitted;
}

}  // namespace foundation::os
