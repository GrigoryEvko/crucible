#pragma once

// The release of the kernel that the raw system calls of the base need, and
// the start-up check of it.
//
// A raw call has no fallback of the C library, so an older kernel gives
// ENOSYS where the C library gave a result.  minimum_kernel() gives the
// largest release that a call of the enumeration Syscall needs on the
// architecture of the build, other than a feature call.  The door file
// src/foundation/os/Syscall.cpp gives the release of each enumerator and
// derives the minimum from them.  A new enumerator with a newer release
// raises the minimum with no other edit.
//
// Feature calls
//   A feature call is a call that the caller of a door chooses, and that no
//   other door needs: renameat2 for the commit that refuses an existing
//   target, copy_file_range for the copy between two files, io_uring_setup
//   for the ring of io_uring, sched_setattr and sched_getattr for the
//   scheduling attributes of fixy/os/Sched.h, mlock2 for the lock of a page
//   when it faults in (fixy/os/Pages.h), and perf_event_open for a perf
//   event of fixy/os/PerfEvent.h.  Its door gives the error enosys
//   of an older kernel to its caller in the Result of the door.  An older
//   kernel then refuses the feature and not the process, so the minimum does
//   not count the release of a feature call.  A kernel with no perf events
//   also gives enosys for perf_event_open.
//
// The start-up check is a function of that door file that the loader runs
// before main, and before the constructors of the other objects, in each
// program and each shared library that links the primitive.  It reads the
// release of the running kernel one time with uname.  An older kernel, or a
// release text that does not start with MAJOR.MINOR, ends the process with
// a message on standard error that names the two releases.  The check keeps
// the release that it read for running_kernel(), and holds no other state.
//
// Flags
//   A flag is a word that a door passes in an argument of a call, such as an
//   advice of madvise, a flag of the setup of a ring of io_uring, or the
//   name of a socket option.  A kernel older than the release of a flag
//   refuses the call, often with einval, far from its cause.  So each flag
//   that the doors of foundation/os and fixy/os pass has a row of
//   flag_floor: the release that added the word, the need of its door, and
//   the fallback of the caller.  A word that came before 2.6.0, the first
//   release of x86_64, has no row, because each release of the base has it.
//   - A flag that a door needs in each call is a row of the need each_call.
//     minimum_kernel() counts its release, so the start-up check ends the
//     process on a kernel that lacks it, with the text of the floor.
//   - A flag that the caller of a door chooses is a row of the need
//     fallback.  Its door gives the error of the kernel to its caller in the
//     Result of the door, and the row states what the caller does then.  The
//     start-up check does not refuse such a row, and the process runs.  The
//     init door of foundation/effects/Effect.h states each such row that the
//     running kernel lacks, one time in the process, through
//     report_kernel_fallbacks_once.  The load of a program or of a shared
//     library writes nothing, so a library in a host process stays silent
//     until the host opens the init door.
//   judge_flag gives the verdict of one row for a release.  Each row above
//   the floor is a fallback row, so a new feature of the kernel does not
//   raise the release that the base needs.  The door file of each word
//   compares the word with its kernel header, and the rows give only the
//   releases.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

#include <compare>
#include <cstdint>

namespace foundation::os {

// A release of the kernel, as uname gives it: MAJOR.MINOR.PATCH.
struct KernelVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;

    friend constexpr std::strong_ordering operator<=>(KernelVersion const&, KernelVersion const&) noexcept = default;
};

// The largest release that a call of the enumeration Syscall, or a flag
// that a door needs in each call, needs on the architecture of the build.
[[nodiscard]] KernelVersion minimum_kernel() noexcept;

// The release of the running kernel that the start-up check read with uname,
// in this program or this shared library.  It is the zero release before the
// check runs, and the check ends the process on a release that it refuses.
[[nodiscard]] KernelVersion running_kernel() noexcept;

// Writes to standard error the text of format_kernel_fallbacks of
// foundation/os/KernelFallbacks.h for the release running, one time in the
// process.  One cell of the process holds the fact of the report, so only
// the first call writes, in the program or in any of its shared libraries,
// and each later call writes nothing.  The first call also spends the report
// when the release lacks no word.  The init door calls it with
// running_kernel(), and a test calls it with a release that lacks rows.
void report_kernel_fallbacks_once(KernelVersion running) noexcept;

// A flag that a door of the base passes to the kernel.  The comment of each
// enumerator names the word of the kernel header, and the door that passes
// it.
enum class KernelFlag : std::uint8_t {
    // O_CLOEXEC of each open of the file doors.
    open_close_on_exec,
    // O_SYNC with the bit of a full sync, of a durable open of fixy/os/Fs.h.
    open_full_sync,
    // O_PATH of the open of a directory of the file doors.
    open_path,
    // O_TMPFILE of the open of a file with no name of fixy/os/Fs.h.
    open_temporary,
    // AT_FDCWD and AT_SYMLINK_NOFOLLOW of the calls of the *at family.
    at_family_words,
    // RESOLVE_BENEATH and RESOLVE_NO_MAGICLINKS of each open with openat2.
    resolve_beneath,
    // MAP_HUGETLB of a map of huge pages of fixy/os/Mmap.h.
    map_huge_tlb,
    // MAP_HUGE_2MB, the size of the huge pages of such a map.
    map_huge_2mb,
    // MAP_FIXED_NOREPLACE of the fixed map of foundation/os/GuardedRegion.h.
    map_fixed_no_replace,
    // MADV_REMOVE of the release of a range of a shared map.
    advise_remove,
    // MADV_HUGEPAGE and MADV_NOHUGEPAGE of fixy/os/Pages.h.
    advise_huge_page,
    // MADV_DONTDUMP of a map that a core dump must not hold.
    advise_dont_dump,
    // MADV_FREE of the lazy release of a range.
    advise_free,
    // MADV_WIPEONFORK of a range that a child process gets as zeros.
    advise_wipe_on_fork,
    // MADV_POPULATE_WRITE of the populate door of fixy/os/Pages.h and of the
    // populate of the page source door of foundation/os/PageSource.h.
    advise_populate_write,
    // MADV_COLLAPSE of the collapse door of fixy/os/Pages.h and of the
    // collapse of the page source door.
    advise_collapse,
    // MLOCK_ONFAULT of the lock door of fixy/os/Pages.h.
    lock_on_fault,
    // MPOL_BIND and MPOL_MF_STRICT of the memory policy door.
    memory_policy_bind,
    // MPOL_MF_MOVE of the memory policy door.
    memory_policy_move,
    // MPOL_PREFERRED and MPOL_INTERLEAVE of the bind of the page source door
    // of foundation/os/PageSource.h.
    memory_policy_preferred,
    // FUTEX_PRIVATE_FLAG of each futex call.
    futex_private,
    // FUTEX_WAIT_BITSET of the wait of the futex door.
    futex_wait_bitset,
    // SCHED_BATCH of the scheduling doors of fixy/os/Sched.h.
    sched_batch,
    // SCHED_IDLE of the scheduling doors.
    sched_idle,
    // SCHED_DEADLINE of the scheduling attributes.
    sched_deadline,
    // PR_SET_THP_DISABLE and PR_GET_THP_DISABLE of the THP door of
    // fixy/os/Pages.h.
    thp_setting,
    // The flags of a refusal of huge pages, such as
    // PR_THP_DISABLE_EXCEPT_ADVISED, which the THP door passes as the third
    // argument of PR_SET_THP_DISABLE.
    thp_refusal_flags,
    // RENAME_NOREPLACE of the commit of fixy/os/Fs.h.
    rename_no_replace,
    // PERF_FLAG_FD_CLOEXEC of fixy/os/PerfEvent.h.
    perf_close_on_exec,
    // SOCK_CLOEXEC of the socket doors of fixy/os/Socket.h.
    socket_close_on_exec,
    // SO_TIMESTAMPING, SCM_TIMESTAMPING and SIOCSHWTSTAMP of the timestamp
    // doors of fixy/os/Socket.h.
    timestamping,
    // SO_MAX_PACING_RATE of the pacing door.
    max_pacing_rate,
    // TCP_CONGESTION of the TCP option doors.
    tcp_congestion,
    // TCP_CC_INFO of the TCP option doors.
    tcp_congestion_info,
    // TCP_RTO_MIN_US of the TCP option doors.
    tcp_rto_min_us,
    // IORING_SETUP_IOPOLL and IORING_SETUP_SQPOLL of fixy/os/Io.h.
    ring_poll,
    // IORING_SETUP_CQSIZE of a ring with its own count of completions.
    ring_completion_size,
    // IORING_SETUP_COOP_TASKRUN of fixy/os/Io.h.
    ring_coop_taskrun,
    // IORING_SETUP_SINGLE_ISSUER of fixy/os/Io.h.
    ring_single_issuer,
    // IORING_SETUP_DEFER_TASKRUN of fixy/os/Io.h.
    ring_defer_taskrun,
    // CLOCK_PROCESS_CPUTIME_ID and CLOCK_THREAD_CPUTIME_ID of the clock
    // doors of fixy/os/Time.h.
    clock_cpu_time,
    // CLOCK_MONOTONIC_RAW of the clock doors.
    clock_monotonic_raw,
    // CLOCK_BOOTTIME of the clock doors.
    clock_boottime,
    // The clock of a descriptor of a PTP device (FD_TO_CLOCKID).
    clock_of_descriptor,
    // PTP_CLOCK_GETCAPS2 of the capabilities of a PTP clock.
    ptp_clock_caps,
};

// What a door does on a kernel that lacks the word of a row.
enum class FlagNeed : std::uint8_t {
    // A door passes the word in each call, and it cannot work without it.
    // minimum_kernel() counts the release of the row.
    each_call,
    // A caller of a door chooses the word.  The door gives the error of the
    // kernel to its caller, and the row states what the caller does then.
    fallback,
};

// The row of one flag: the release that added its word, the need of its
// door, the name of the word, and the fallback of the caller.  Each text is
// a string literal with its zero.  A row of the need each_call has an empty
// fallback text.
struct FlagFloor {
    KernelVersion release{};
    FlagNeed need = FlagNeed::each_call;
    char const* word = "";
    char const* fallback = "";
};

// The row of FLAG.  The releases come from the kernel history.  An
// enumerator with no row gives the empty row of the default, which has no
// word, and the walk of the rows in test_os_floor refuses it.
[[nodiscard]] constexpr FlagFloor flag_floor(KernelFlag flag) noexcept {
    switch (flag) {
        case KernelFlag::open_close_on_exec:
            return FlagFloor{.release = {2, 6, 23}, .need = FlagNeed::each_call, .word = "O_CLOEXEC"};
        case KernelFlag::open_full_sync:
            return FlagFloor{.release = {2, 6, 33},
                             .need = FlagNeed::fallback,
                             .word = "O_SYNC",
                             .fallback = "An older kernel reads the word as O_DSYNC and gives no error. A write then "
                                         "waits for its data, and not for each change of the metadata."};
        case KernelFlag::open_path:
            return FlagFloor{.release = {2, 6, 39}, .need = FlagNeed::each_call, .word = "O_PATH"};
        case KernelFlag::open_temporary:
            return FlagFloor{.release = {3, 11, 0},
                             .need = FlagNeed::fallback,
                             .word = "O_TMPFILE",
                             .fallback = "The open gives the error of the kernel. The caller writes a file with a "
                                         "name and renames it."};
        case KernelFlag::at_family_words:
            return FlagFloor{
                .release = {2, 6, 16}, .need = FlagNeed::each_call, .word = "AT_FDCWD, AT_SYMLINK_NOFOLLOW"};
        case KernelFlag::resolve_beneath:
            return FlagFloor{
                .release = {5, 6, 0}, .need = FlagNeed::each_call, .word = "RESOLVE_BENEATH, RESOLVE_NO_MAGICLINKS"};
        case KernelFlag::map_huge_tlb:
            return FlagFloor{.release = {2, 6, 32},
                             .need = FlagNeed::fallback,
                             .word = "MAP_HUGETLB",
                             .fallback = "The map gives the error of the kernel. The caller maps pages of the base "
                                         "size."};
        case KernelFlag::map_huge_2mb:
            return FlagFloor{.release = {3, 8, 0},
                             .need = FlagNeed::fallback,
                             .word = "MAP_HUGE_2MB",
                             .fallback = "The map gives the error of the kernel. The caller maps pages of the base "
                                         "size."};
        case KernelFlag::map_fixed_no_replace:
            return FlagFloor{.release = {4, 17, 0}, .need = FlagNeed::each_call, .word = "MAP_FIXED_NOREPLACE"};
        case KernelFlag::advise_remove:
            return FlagFloor{.release = {2, 6, 16},
                             .need = FlagNeed::fallback,
                             .word = "MADV_REMOVE",
                             .fallback = "The advice gives the error of the kernel. The range keeps its pages until "
                                         "the unmap."};
        case KernelFlag::advise_huge_page:
            return FlagFloor{.release = {2, 6, 38},
                             .need = FlagNeed::fallback,
                             .word = "MADV_HUGEPAGE, MADV_NOHUGEPAGE",
                             .fallback = "The advice gives the error of the kernel. The range keeps the THP setting "
                                         "of the system."};
        case KernelFlag::advise_dont_dump:
            return FlagFloor{.release = {3, 4, 0},
                             .need = FlagNeed::fallback,
                             .word = "MADV_DONTDUMP",
                             .fallback = "The advice gives the error of the kernel. A core dump holds the range."};
        case KernelFlag::advise_free:
            return FlagFloor{.release = {4, 5, 0},
                             .need = FlagNeed::fallback,
                             .word = "MADV_FREE",
                             .fallback = "The advice gives the error of the kernel. The caller releases the range "
                                         "with MADV_DONTNEED."};
        case KernelFlag::advise_wipe_on_fork:
            return FlagFloor{.release = {4, 14, 0},
                             .need = FlagNeed::fallback,
                             .word = "MADV_WIPEONFORK",
                             .fallback = "The advice gives the error of the kernel. A child process gets a copy of "
                                         "the range."};
        case KernelFlag::advise_populate_write:
            return FlagFloor{.release = {5, 14, 0},
                             .need = FlagNeed::fallback,
                             .word = "MADV_POPULATE_WRITE",
                             .fallback = "The populate door gives einval. The prefault of the warden writes a "
                                         "warning, and the first write of each page faults it in. The populate of "
                                         "the page source faults in each page with an atomic or of zero."};
        case KernelFlag::advise_collapse:
            return FlagFloor{.release = {6, 1, 0},
                             .need = FlagNeed::fallback,
                             .word = "MADV_COLLAPSE",
                             .fallback = "The collapse door gives einval, and the collapse of the page source gives "
                                         "the base pages. The range keeps its pages of the base size until the THP "
                                         "daemon collapses them."};
        case KernelFlag::lock_on_fault:
            return FlagFloor{.release = {4, 4, 0},
                             .need = FlagNeed::fallback,
                             .word = "MLOCK_ONFAULT",
                             .fallback = "The lock gives the error of the kernel. The caller locks the range with "
                                         "mlock, which faults in each page."};
        case KernelFlag::memory_policy_bind:
            return FlagFloor{.release = {2, 6, 7}, .need = FlagNeed::each_call, .word = "MPOL_BIND, MPOL_MF_STRICT"};
        case KernelFlag::memory_policy_move:
            return FlagFloor{.release = {2, 6, 16}, .need = FlagNeed::each_call, .word = "MPOL_MF_MOVE"};
        case KernelFlag::memory_policy_preferred:
            return FlagFloor{.release = {2, 6, 7},
                             .need = FlagNeed::fallback,
                             .word = "MPOL_PREFERRED, MPOL_INTERLEAVE",
                             .fallback = "The bind of the page source keeps the policy of the thread and gives the "
                                         "unknown placement."};
        case KernelFlag::futex_private:
            return FlagFloor{.release = {2, 6, 22}, .need = FlagNeed::each_call, .word = "FUTEX_PRIVATE_FLAG"};
        case KernelFlag::futex_wait_bitset:
            return FlagFloor{.release = {2, 6, 25}, .need = FlagNeed::each_call, .word = "FUTEX_WAIT_BITSET"};
        case KernelFlag::sched_batch:
            return FlagFloor{.release = {2, 6, 16},
                             .need = FlagNeed::fallback,
                             .word = "SCHED_BATCH",
                             .fallback = "The scheduling mint gives einval. The thread keeps its policy."};
        case KernelFlag::sched_idle:
            return FlagFloor{.release = {2, 6, 23},
                             .need = FlagNeed::fallback,
                             .word = "SCHED_IDLE",
                             .fallback = "The scheduling mint gives einval. The thread keeps its policy."};
        case KernelFlag::sched_deadline:
            return FlagFloor{.release = {3, 14, 0},
                             .need = FlagNeed::fallback,
                             .word = "SCHED_DEADLINE",
                             .fallback = "The scheduling mint gives the error of the kernel. The thread keeps its "
                                         "policy."};
        case KernelFlag::thp_setting:
            return FlagFloor{.release = {3, 15, 0},
                             .need = FlagNeed::fallback,
                             .word = "PR_SET_THP_DISABLE, PR_GET_THP_DISABLE",
                             .fallback = "The THP door gives einval. The process keeps the THP setting of the "
                                         "system."};
        case KernelFlag::thp_refusal_flags:
            return FlagFloor{.release = {6, 18, 0},
                             .need = FlagNeed::fallback,
                             .word = "PR_THP_DISABLE_EXCEPT_ADVISED",
                             .fallback = "The THP door gives einval and changes nothing. The caller refuses huge "
                                         "pages with no flag."};
        case KernelFlag::rename_no_replace:
            return FlagFloor{.release = {3, 15, 0},
                             .need = FlagNeed::fallback,
                             .word = "RENAME_NOREPLACE",
                             .fallback = "The commit gives the error of the kernel. The caller commits with a "
                                         "rename that can replace the target."};
        case KernelFlag::perf_close_on_exec:
            return FlagFloor{.release = {3, 14, 0},
                             .need = FlagNeed::fallback,
                             .word = "PERF_FLAG_FD_CLOEXEC",
                             .fallback = "The perf door gives einval. The process has no perf events."};
        case KernelFlag::socket_close_on_exec:
            return FlagFloor{.release = {2, 6, 27}, .need = FlagNeed::each_call, .word = "SOCK_CLOEXEC"};
        case KernelFlag::timestamping:
            return FlagFloor{.release = {2, 6, 30},
                             .need = FlagNeed::fallback,
                             .word = "SO_TIMESTAMPING, SCM_TIMESTAMPING, SIOCSHWTSTAMP",
                             .fallback = "The timestamp door gives the error of the kernel. The caller reads the "
                                         "time of the software."};
        case KernelFlag::max_pacing_rate:
            return FlagFloor{.release = {3, 13, 0},
                             .need = FlagNeed::fallback,
                             .word = "SO_MAX_PACING_RATE",
                             .fallback = "The pacing door gives the error of the kernel. The socket sends with no "
                                         "limit of its rate."};
        case KernelFlag::tcp_congestion:
            return FlagFloor{.release = {2, 6, 13},
                             .need = FlagNeed::fallback,
                             .word = "TCP_CONGESTION",
                             .fallback = "The option door gives enoprotoopt. The socket keeps the congestion "
                                         "control of the system."};
        case KernelFlag::tcp_congestion_info:
            return FlagFloor{.release = {4, 1, 0},
                             .need = FlagNeed::fallback,
                             .word = "TCP_CC_INFO",
                             .fallback = "The option door gives enoprotoopt. The caller reads TCP_INFO only."};
        case KernelFlag::tcp_rto_min_us:
            return FlagFloor{.release = {6, 15, 0},
                             .need = FlagNeed::fallback,
                             .word = "TCP_RTO_MIN_US",
                             .fallback = "The option door gives enoprotoopt. The socket keeps the minimum "
                                         "retransmission time of its route and of the system."};
        case KernelFlag::ring_poll:
            return FlagFloor{.release = {5, 1, 0},
                             .need = FlagNeed::fallback,
                             .word = "IORING_SETUP_IOPOLL, IORING_SETUP_SQPOLL",
                             .fallback = "The ring mint gives the error of the kernel. The caller sets up a ring "
                                         "with fewer flags."};
        case KernelFlag::ring_completion_size:
            return FlagFloor{.release = {5, 5, 0},
                             .need = FlagNeed::fallback,
                             .word = "IORING_SETUP_CQSIZE",
                             .fallback = "The ring mint gives einval. The caller sets up a ring with the count of "
                                         "completions that the kernel gives."};
        case KernelFlag::ring_coop_taskrun:
            return FlagFloor{.release = {5, 19, 0},
                             .need = FlagNeed::fallback,
                             .word = "IORING_SETUP_COOP_TASKRUN",
                             .fallback = "The ring mint gives einval. The caller sets up a ring with fewer flags."};
        case KernelFlag::ring_single_issuer:
            return FlagFloor{.release = {6, 0, 0},
                             .need = FlagNeed::fallback,
                             .word = "IORING_SETUP_SINGLE_ISSUER",
                             .fallback = "The ring mint gives einval. The caller sets up a ring with fewer flags."};
        case KernelFlag::ring_defer_taskrun:
            return FlagFloor{.release = {6, 1, 0},
                             .need = FlagNeed::fallback,
                             .word = "IORING_SETUP_DEFER_TASKRUN",
                             .fallback = "The ring mint gives einval. The caller sets up a ring with fewer flags."};
        case KernelFlag::clock_cpu_time:
            return FlagFloor{.release = {2, 6, 12},
                             .need = FlagNeed::fallback,
                             .word = "CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID",
                             .fallback = "The clock door gives einval. The caller reads CLOCK_MONOTONIC."};
        case KernelFlag::clock_monotonic_raw:
            return FlagFloor{.release = {2, 6, 28},
                             .need = FlagNeed::fallback,
                             .word = "CLOCK_MONOTONIC_RAW",
                             .fallback = "The clock door gives einval. The caller reads CLOCK_MONOTONIC."};
        case KernelFlag::clock_boottime:
            return FlagFloor{.release = {2, 6, 39},
                             .need = FlagNeed::fallback,
                             .word = "CLOCK_BOOTTIME",
                             .fallback = "The clock door gives einval. The caller reads CLOCK_MONOTONIC, which "
                                         "stops while the system sleeps."};
        case KernelFlag::clock_of_descriptor:
            return FlagFloor{.release = {2, 6, 39},
                             .need = FlagNeed::fallback,
                             .word = "FD_TO_CLOCKID",
                             .fallback = "The PTP clock door gives einval. The caller reads CLOCK_REALTIME."};
        case KernelFlag::ptp_clock_caps:
            return FlagFloor{.release = {5, 4, 0},
                             .need = FlagNeed::fallback,
                             .word = "PTP_CLOCK_GETCAPS2",
                             .fallback = "The capability read gives the error of the kernel. The caller uses the "
                                         "clock with no capability of the device."};
        default:
            break;
    }
    return FlagFloor{};
}

// The verdict of one row for the release RUNNING.
enum class FlagVerdict : std::uint8_t {
    // The release has the word.
    admitted,
    // The release lacks the word, and its door gives the error of the kernel.
    // The caller takes the fallback of the row.
    fallback,
    // The release lacks a word that a door needs in each call.  The start-up
    // check refuses such a release, because minimum_kernel() counts the row.
    refused,
};

// The verdict of the row of FLAG for the release RUNNING.  The patch number
// counts: a release that added a word in a stable patch is newer than the
// release before it.  Complexity: O(1).
[[nodiscard]] constexpr FlagVerdict judge_flag(KernelFlag flag, KernelVersion running) noexcept {
    FlagFloor const row = flag_floor(flag);
    if (!(running < row.release)) {
        return FlagVerdict::admitted;
    }
    return row.need == FlagNeed::fallback ? FlagVerdict::fallback : FlagVerdict::refused;
}

// The verdict of the start-up check for one release text.
enum class KernelVerdict : std::uint8_t {
    admitted,
    too_old,
    unreadable,
};

// The verdict for the release text RELEASE against minimum_kernel().  The
// check reads RELEASE up to its zero, and at most 64 characters, which is the
// length of the release field of uname.  A text that does not start with
// MAJOR.MINOR is unreadable, and so is a part that does not fit in 16 bits.
// A missing patch number, or a patch number that a suffix follows, reads as
// written: "6.1" is 6.1.0, and "7.1.4-202.fc44" is 7.1.4.
[[nodiscard]] KernelVerdict judge_release(char const* release) noexcept;

}  // namespace foundation::os
