// The door of the raw system call primitive (foundation/os/Syscall.h) and of
// the kernel floor (foundation/os/KernelFloor.h).
//
// This file is the one place of the base with the system call instruction,
// and the one includer of <asm/unistd.h>, <asm-generic/errno-base.h>,
// <linux/fcntl.h>, <linux/futex.h>, <linux/time_types.h> and
// <linux/utsname.h>.  It includes no header of the C library, because some
// declarations of the two kinds of header conflict.  In a build with
// ThreadSanitizer, the primitive gives a read or a write that can block to
// src/foundation/os/Intercepted.cpp, which is the one door of the floor that
// calls the C library.  It issues a futex call raw, between the hooks of that
// file.

#include <foundation/os/FutexWords.h>
#include <foundation/os/KernelFloor.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <asm-generic/errno-base.h>
#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
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
static_assert(kOpenReadOnly == O_RDONLY, "kOpenReadOnly is not O_RDONLY of <linux/fcntl.h>");
static_assert(kOpenCloseOnExec == O_CLOEXEC, "kOpenCloseOnExec is not O_CLOEXEC of <linux/fcntl.h>");
static_assert(kOpenDirectory == O_DIRECTORY, "kOpenDirectory is not O_DIRECTORY of <linux/fcntl.h>");

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
// architecture and the release that added the call.  openat came in 2.6.16.
// futex came in 2.6.0, and the futex door uses FUTEX_WAIT_BITSET, which came
// in 2.6.25.
[[nodiscard]] constexpr KernelVersion introduced_in(Syscall call) noexcept {
    KernelVersion added = kFirstRelease;
    switch (call) {
        case Syscall::openat:
            added = KernelVersion{2, 6, 16};
            break;
        case Syscall::futex:
            added = KernelVersion{2, 6, 25};
            break;
        case Syscall::read:
        case Syscall::write:
        case Syscall::close:
        case Syscall::uname:
        case Syscall::getdents64:
        case Syscall::sched_getaffinity:
        default:
            break;
    }
    return added > kFirstRelease ? added : kFirstRelease;
}

// The largest release that an enumerator of Syscall needs.  The walk reads
// each enumerator, so a new call joins the derivation with no other edit.
[[nodiscard]] consteval KernelVersion derive_minimum_kernel() noexcept {
    KernelVersion largest = kFirstRelease;
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^Syscall)) {
        KernelVersion const needed = introduced_in(std::meta::extract<Syscall>(enumerator));
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
    if (can_block(call)) {
        return detail::issue_intercepted(call, first.bits(), second.bits(), third.bits());
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
