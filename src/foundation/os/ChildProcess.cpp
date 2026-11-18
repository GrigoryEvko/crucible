// The child door of the base (foundation/os/ChildProcess.h).  pipe2, dup3,
// read and close go through the raw primitive of foundation/os/Syscall.h, and
// fork, waitpid and _exit go through the floor of
// src/foundation/os/Intercepted.cpp.  So this file includes no system header.

#include <foundation/os/ChildProcess.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include "Intercepted.h"

#include <cstddef>
#include <cstdint>

namespace foundation::os::detail {

namespace {

// The descriptor of the standard error of a process.
constexpr std::int32_t kStandardError = 2;

// The option of prctl that sets the dumpable flag of the process
// (PR_SET_DUMPABLE), and the value of the flag that refuses each core dump.
// <linux/prctl.h> gives them, and no door owns that header.  The test of the
// child door reads the flag in a child, so a wrong value fails it.
constexpr int kSetDumpable = 4;
constexpr int kNotDumpable = 0;

// The two ends of a pipe, as pipe2 writes them: the read end, then the write
// end.
struct PipeEnds {
    std::int32_t read_end = -1;
    std::int32_t write_end = -1;
};

void close_descriptor(std::int32_t descriptor) noexcept { static_cast<void>(raw_syscall(Syscall::close, descriptor)); }

[[nodiscard]] ChildEnd child_end_of(ProcessStatus const& status) noexcept {
    ChildEndKind const kind = status.end == ProcessEnd::signaled ? ChildEndKind::signaled : ChildEndKind::exited;
    return ChildEnd{kind, status.number, status.has_core_dump};
}

}  // namespace

core::Result<ChildStart, Errno> start_child_() noexcept {
    PipeEnds ends{};
    long const opened = raw_syscall(Syscall::pipe2, static_cast<void const*>(&ends), kOpenCloseOnExec);
    if (opened < 0) {
        return core::err(static_cast<Errno>(-opened));
    }
    long const child = fork_process();
    if (child < 0) {
        close_descriptor(ends.read_end);
        close_descriptor(ends.write_end);
        return core::err(static_cast<Errno>(-child));
    }
    if (child == 0) {
        // dup3 with no flag clears O_CLOEXEC on the copy, so the standard
        // error of the child stays open.
        long const moved = raw_syscall(Syscall::dup3, ends.write_end, kStandardError, 0);
        close_descriptor(ends.read_end);
        close_descriptor(ends.write_end);
        // THE CORE DUMP of foundation/os/ChildProcess.h.
        long const undumped = raw_syscall(Syscall::prctl, kSetDumpable, kNotDumpable);
        if (moved < 0 || undumped < 0) {
            exit_process(kChildSetupFailed);
        }
        return ChildStart{0, -1};
    }
    close_descriptor(ends.write_end);
    return ChildStart{static_cast<std::int32_t>(child), ends.read_end};
}

core::Result<std::size_t, Errno> read_child_output_(std::int32_t error_output, core::View<char> bytes) noexcept {
    char* const first = first_of_(core::detail::ViewDoor::key(), bytes);
    long taken = raw_syscall(Syscall::read, error_output, first, bytes.size());
    while (taken == -static_cast<long>(Errno::eintr)) {
        taken = raw_syscall(Syscall::read, error_output, first, bytes.size());
    }
    if (taken < 0) {
        return core::err(static_cast<Errno>(-taken));
    }
    return static_cast<std::size_t>(taken);
}

void close_child_output_(std::int32_t error_output) noexcept { close_descriptor(error_output); }

core::Result<ChildEnd, Errno> wait_child_(std::int32_t child) noexcept {
    ProcessStatus status{};
    long waited = wait_process(child, status);
    while (waited == -static_cast<long>(Errno::eintr)) {
        waited = wait_process(child, status);
    }
    if (waited < 0) {
        return core::err(static_cast<Errno>(-waited));
    }
    return child_end_of(status);
}

void exit_child_(std::uint8_t code) noexcept { exit_process(code); }

}  // namespace foundation::os::detail

namespace foundation::os {

ChildEnd decode_child_end(std::int32_t status_word) noexcept {
    return detail::child_end_of(detail::decode_process_status(status_word));
}

}  // namespace foundation::os
