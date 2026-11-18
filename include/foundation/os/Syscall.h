#pragma once

// The raw system call primitive of the base.
//
// THE PRIMITIVE
//   raw_syscall issues one system call that the enumeration Syscall names,
//   with at most six argument words.  It returns the raw signed word of the
//   kernel: a value, or the error number E as the word -E, with E from 1 to
//   4095.  The door file src/foundation/os/Syscall.cpp holds the one
//   instruction of each architecture, syscall on x86_64 and svc #0 on
//   aarch64.  It takes the number of each call from <asm/unistd.h>, so an
//   enumerator names a call and not a number, and one enumerator means one
//   call on the two architectures.  A door of include/foundation/os reads
//   the word into a Result of the base (foundation/os/Errno.h).
//
//   An argument word can be an address that the kernel reads or writes, so
//   the primitive is not safe.  Only a door issues it, and the door gives a
//   safe operation.  An owner row of the rule syscalls in
//   utils/rule-table.txt admits a call of raw_syscall only in the door
//   directories: include/foundation/os, src/foundation/os, include/fixy/os
//   and src/fixy/os.  Each call is also the kind raw-syscall of that rule,
//   and the ledger of the rule gives the capability claim of each call.
//
//   The deny rows of the rule table hold each system call that the base does
//   not issue raw, because the C library owns its state: the signal calls, the
//   thread and process starts, the thread and TLS state, the credentials, the
//   program break and the exits.  The enumeration names none of them, and the
//   rule syscalls refuses a raw call of one (denied-syscall).  The one
//   exception is the block of SIGPIPE of foundation/os/PipeSignal.h: the door
//   file issues rt_sigprocmask and rt_sigtimedwait with a set that holds only
//   SIGPIPE, and with no enumerator.
//
// THE SANITIZERS
//   ThreadSanitizer delivers a signal only inside a call that it intercepts,
//   or inside a call that it knows to block.  A raw call that can block
//   (can_block) is out of its sight, and no handler of a signal then runs
//   while the call waits.  So in a build with ThreadSanitizer, the primitive
//   tells the sanitizer of each such call, in one of two forms:
//     - read and write go to the function of the C library in
//       src/foundation/os/Intercepted.cpp, which the sanitizer intercepts
//     - futex stays raw, between the two system call hooks of the sanitizer
//       for futex.  The C library has no function for futex.  The hooks
//       mark the thread as in a blocking call, so a handler runs at once.
//       The route also tells the sanitizer that a wake publishes the writes
//       of the thread, and that a wait that ended with no error sees them.
//     - clock_nanosleep stays raw (the owner decision of 2026-11-14).  The
//       sanitizer does not intercept the function of the C library.  The
//       hooks of clock_nanosleep check the read of the deadline and the
//       write of the time that remains, and they mark no blocking call.  The
//       hook pair of futex is the one public pair that marks a blocking
//       call, and its two hooks read no word.  So the sleep goes between the
//       blocking marks of the futex hooks, inside the hooks of
//       clock_nanosleep.
//   The sanitizer keeps a shadow of each address that a thread reads or
//   writes, and a raw unmap that it does not see leaves the shadow of the
//   range for the next map of the same range.  Only its interceptors of mmap
//   and munmap reset the shadow, and the system call hooks of the two calls
//   are empty.  So mmap and munmap go to the functions of the C library in
//   src/foundation/os/Intercepted.cpp (the owner decision of 2026-11-14).
//   mprotect and madvise change no state of the sanitizer, and they stay raw.
//   The sanitizer also keeps a state for each descriptor number, and a raw
//   close that it does not see leaves that state for the next descriptor
//   with the same number.  So close, openat and openat2 stay raw, between
//   the system call hooks of the sanitizer for each call.  The hook of close
//   checks the close against the uses of the descriptor in other threads,
//   and then it clears the state.  The hooks of the two opens check the read
//   of the path.  The sanitizer intercepts neither open of the *at family,
//   and no hook gives the state of a new descriptor.
//   Each other build and each other call takes the raw path.  This is rule
//   R18 of misc/01_10_2026_quarantine.md.
//
// This header is in the floor layer F0_os of the rule table, below the
// families of foundation/core, so the Report family and each door can use it.
// It names no type of the base, of the C library or of the kernel.
// foundation/os/KernelFloor.h gives the release of the kernel that the raw
// calls need, and the start-up check of it.

#include <cstdint>
#include <type_traits>

namespace foundation::os {

// The system calls that a door of the base issues raw.  An enumerator names a
// call, and the door file gives its number on the architecture of the build.
// A call joins the enumeration only when a door issues it.
enum class Syscall : std::uint8_t {
    read,
    write,
    openat,
    close,
    uname,
    getdents64,
    sched_getaffinity,
    futex,
    mmap,
    mprotect,
    munmap,
    fsync,
    fdatasync,
    fstat,
    renameat,
    renameat2,
    unlinkat,
    mkdirat,
    newfstatat,
    madvise,
    mincore,
    sendfile,
    copy_file_range,
    io_uring_setup,
    ioctl,
    sched_setaffinity,
    sched_setscheduler,
    sched_setattr,
    setpriority,
    mbind,
    move_pages,
    get_mempolicy,
    set_mempolicy,
    getpid,
    setsockopt,
    getsockopt,
    bind,
    sendto,
    recvfrom,
    recvmsg,
    sched_getattr,
    mlock,
    mlock2,
    munlock,
    prctl,
    gettid,
    perf_event_open,
    openat2,
    socket,
    pipe2,
    dup3,
    clock_gettime,
    clock_nanosleep,
};

// True when the call can wait for a long time: for another thread or process
// with no bound, for example a read of an empty pipe, a write to a full one,
// or a futex wait, or to a deadline, as clock_nanosleep does.  In a build
// with ThreadSanitizer, the primitive tells the sanitizer of such a call (THE
// SANITIZERS).  A clock_gettime does not wait.  An ioctl waits only for a
// request that waits, and the doors of fixy/os/Fd.h and fixy/os/Socket.h send
// only a request that the kernel answers at once.  The socket door of
// fixy/os/Socket.h passes MSG_DONTWAIT to each sendto, recvfrom and recvmsg,
// so none of them waits.
[[nodiscard]] constexpr bool can_block(Syscall call) noexcept {
    return call == Syscall::read || call == Syscall::write || call == Syscall::futex
        || call == Syscall::clock_nanosleep;
}

// One argument word of a system call: an integer, or the address of memory
// that the call reads or writes.  A signed integer keeps its sign in the
// word, as the kernel reads an int argument such as AT_FDCWD.
class Word final {
public:
    constexpr Word() noexcept = default;

    template <class Integer>
        requires std::is_integral_v<Integer> && (!std::is_same_v<Integer, bool>)
    constexpr Word(Integer value) noexcept : bits_{static_cast<std::uint64_t>(value)} {}

    Word(void const* address) noexcept : bits_{__builtin_bit_cast(std::uint64_t, address)} {}

    [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

private:
    std::uint64_t bits_ = 0;
};

// Issues one system call (THE PRIMITIVE), and returns the raw signed word of
// the kernel.  The words that the call does not read can stay empty.
[[nodiscard]] long raw_syscall(Syscall call, Word first = {}, Word second = {}, Word third = {}, Word fourth = {},
                               Word fifth = {}, Word sixth = {}) noexcept;

}  // namespace foundation::os
