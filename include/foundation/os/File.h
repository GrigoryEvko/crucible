#pragma once

// The file door of the base: a read of one file into a buffer of the caller.
//
// read_file opens the file at its path with openat, reads it from its start
// until the end of the file or a full buffer, and closes it.  No descriptor
// leaves the door, so no caller can leak one or close one twice.  A read that
// a signal interrupts is issued again, and each other error of the kernel is
// the error of the Result.  The door is for a small file, such as a file of
// /proc or /sys, which the kernel makes at the read.
//
// The two forms take a mutable region of the Region family.  The View form
// gives the count of the characters that it read, and the FixedText form sets
// the count of the text.
//
// This header is in the layer F0_doors of utils/rule-table.txt.  The door file
// is src/foundation/os/File.cpp, which issues raw_syscall.

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>

#include <cstddef>

namespace foundation::os {

// Reads the file PATH from its start into BUFFER, until the end of the file
// or a full buffer.  The value is the count of the characters read.
[[nodiscard]] core::Result<std::size_t, Errno> read_file(core::CText path, core::View<char> buffer) noexcept;

// Reads the file PATH from its start into TEXT, until the end of the file or
// Capacity characters.  The text then holds the characters read.  On an
// error, the text holds no character.
template <std::size_t Capacity>
[[nodiscard]] core::Result<core::Unit, Errno> read_file(core::CText path, core::FixedText<Capacity>& text) noexcept {
    core::detail::text_resize_(text, 0);
    core::View<char> const buffer = core::detail::view_over_(core::detail::text_bytes_(text), Capacity);
    return read_file(path, buffer)
        .match(
            [&text](std::size_t count) -> core::Result<core::Unit, Errno> {
                core::detail::text_resize_(text, count);
                return core::Unit{};
            },
            [](Errno error) -> core::Result<core::Unit, Errno> { return core::err(error); });
}

}  // namespace foundation::os
