// The file door of the base (foundation/os/File.h).  It issues the raw
// primitive, and foundation/os/OpenFlags.h gives the words of the open.

#include <foundation/os/File.h>

#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

namespace {

// The flags of the open.  The close on exec keeps the descriptor out of a
// program that a thread of the process starts before the close.
constexpr int kReadOnly = kOpenReadOnly | kOpenCloseOnExec;

// Reads the open file DESCRIPTOR into BUFFER and closes it.  The close of a
// file that a read opened has no error that the caller can act on, so the
// door does not report one.
[[nodiscard]] core::Result<std::size_t, Errno> read_and_close(int descriptor, core::View<char> buffer) noexcept {
    char* const first = core::detail::first_of_(buffer);
    std::size_t const capacity = buffer.size();
    std::size_t total = 0;
    bool is_at_end = false;
    bool has_failed = false;
    Errno failure = Errno::eio;
    while (!is_at_end && !has_failed && total < capacity) {
        result_of(raw_syscall(Syscall::read, descriptor, first + total, capacity - total))
            .match(
                [&](std::uint64_t taken) {
                    is_at_end = taken == 0;
                    total += static_cast<std::size_t>(taken);
                },
                [&](Errno error) {
                    if (error != Errno::eintr) {
                        failure = error;
                        has_failed = true;
                    }
                });
    }
    static_cast<void>(raw_syscall(Syscall::close, descriptor));
    if (has_failed) {
        return core::err(failure);
    }
    return total;
}

}  // namespace

core::Result<std::size_t, Errno> read_file(core::CText path, core::View<char> buffer) noexcept {
    return result_of(raw_syscall(Syscall::openat, kAtCurrentDirectory, core::detail::c_text_first_(path), kReadOnly))
        .match([&buffer](std::uint64_t descriptor) { return read_and_close(static_cast<int>(descriptor), buffer); },
               [](Errno error) -> core::Result<std::size_t, Errno> { return core::err(error); });
}

}  // namespace foundation::os
