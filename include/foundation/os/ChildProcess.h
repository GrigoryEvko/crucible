#pragma once

// The child door of the base: the steps that start a child process with its
// standard error on a pipe, read that pipe, wait for the end of the child,
// and end a child.
//
// The door file src/foundation/os/ChildProcess.cpp issues pipe2, dup3, read
// and close through the raw primitive of foundation/os/Syscall.h.  It calls
// fork, waitpid and _exit of the C library through the floor of
// src/foundation/os/Intercepted.cpp (decision D12 of
// misc/01_10_2026_quarantine.md).  So this header includes no system header.
//
// The steps take and give raw descriptor words, so each one is in the
// namespace detail.  The process door of fixy/os/Process.h is the one caller.
// It gives the steps a type for the child and a gate of the Test context.
//
// This header is in the layer F0_doors of utils/rule-table.txt.

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

// How a child process ended: by an exit with a code, or by a signal.
enum class ChildEndKind : std::uint8_t {
    exited,
    signaled,
};

// The end of a child process: the exit code or the number of the signal.
// has_core_dump is true when a signal ended the child and the kernel wrote
// its core dump.
struct ChildEnd {
    ChildEndKind kind = ChildEndKind::exited;
    std::uint8_t number = 0;
    bool has_core_dump = false;
};

// The end that a status word of waitpid tells.  A wait of the child door
// gives the end of its word through the same decode.  The decode has no
// effect, so a test can read the decode of a word that no child of the test
// gives, such as a word with the bit of a core dump.
[[nodiscard]] ChildEnd decode_child_end(std::int32_t status_word) noexcept;

// The exit code of a child whose redirection of its standard error failed.
// The child ends before its body runs.
inline constexpr std::uint8_t kChildSetupFailed = 127;

namespace detail {

// The result of a start on the two sides of the fork.  In the parent, child
// is the id of the child, above zero, and error_output is the read end of
// the pipe that holds the standard error of the child.  In the child, child
// is zero and error_output is -1.
struct ChildStart {
    std::int32_t child = 0;
    std::int32_t error_output = -1;
};

// Opens a pipe with O_CLOEXEC, and forks.  The child puts the write end of
// the pipe on its standard error, closes the two ends, and gets ChildStart
// with child zero.  A child whose redirection fails ends at once with
// kChildSetupFailed.  The parent closes the write end and gets the id of
// the child and the read end.  On an error of the pipe or of the fork, the
// parent gets the error and keeps no descriptor.
//
// The child of a process with threads has only the thread that called the
// start.  A lock that a different thread held at the fork stays held in the
// child.  So the code that the child runs must use only what is safe after a
// fork.
[[nodiscard]] core::Result<ChildStart, Errno> start_child_() noexcept;

// Reads the next bytes of the standard error of a child into bytes, again
// after an eintr.  The count is zero at the end of the output, when the
// child and each copy of the write end are closed.
[[nodiscard]] core::Result<std::size_t, Errno> read_child_output_(std::int32_t error_output,
                                                                  core::View<char> bytes) noexcept;

// Closes the read end that start_child_ gave.
void close_child_output_(std::int32_t error_output) noexcept;

// Waits until the child ends, again after an eintr, and gives its end.  The
// wait also removes the child from the process table.
[[nodiscard]] core::Result<ChildEnd, Errno> wait_child_(std::int32_t child) noexcept;

// Ends the calling process with code, and runs no exit hook and no static
// destructor.  The child of a start ends here when its body returns.
[[noreturn]] void exit_child_(std::uint8_t code) noexcept;

}  // namespace detail

}  // namespace foundation::os
