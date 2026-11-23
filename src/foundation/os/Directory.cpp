// The door of a directory listing (foundation/os/Directory.h).
//
// getdents64 writes the entries as records of struct linux_dirent64 of the
// kernel, one after the other:
//
//   offset  0  d_ino      8 bytes, the inode number
//   offset  8  d_off      8 bytes, the position of the next record
//   offset 16  d_reclen   2 bytes, the length of this record
//   offset 18  d_type     1 byte, the type of the entry
//   offset 19  d_name     the name, with a zero after it
//
// The layout is the ABI of the call on each architecture (getdents64(2)).
// No header of the kernel for user space declares the struct, so the door
// reads each field at its offset.  A record whose length leaves no room for
// its fields, or whose name has no zero inside the record, ends the listing
// with the error EIO.

#include <foundation/os/Directory.h>

#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os::detail {

namespace {

constexpr std::size_t kLengthOffset = 16;
constexpr std::size_t kNameOffset = 19;

// The bytes of the records that one getdents64 call can write.  A record of
// the longest name, 255 bytes, takes at most 280 bytes, so each call gives at
// least one record.
constexpr std::size_t kRecordBytes = 4096;

// The answer of one buffer of records.
enum class Walk : std::uint8_t {
    next_buffer,
    stopped,
    malformed,
};

// Gives each name of the count bytes of records at records to visit.
// Complexity: linear in count.
[[nodiscard]] Walk visit_records(char const* records, std::size_t count, EntryVisit visit) noexcept {
    std::size_t offset = 0;
    while (offset < count) {
        if (count - offset <= kNameOffset) return Walk::malformed;
        std::uint16_t length = 0;
        __builtin_memcpy(&length, records + offset + kLengthOffset, sizeof length);
        if (length <= kNameOffset || length > count - offset) return Walk::malformed;
        char const* const name = records + offset + kNameOffset;
        std::size_t const room = length - kNameOffset;
        std::size_t size = 0;
        while (size < room && name[size] != '\0') {
            ++size;
        }
        if (size == room) return Walk::malformed;
        bool const is_self = size == 1 && name[0] == '.';
        bool const is_parent = size == 2 && name[0] == '.' && name[1] == '.';
        if (is_self || is_parent) {
            offset += length;
            continue;
        }
        // The name and its zero are bytes that the kernel wrote, so the CText
        // comes through the foreign door and the check of its zero.
        core::CText const entry = core::mint_c_text(core::mint_foreign_text(name, size + 1).characters())
                                      .expect("the walk of the record found the zero after the name");
        if (visit.call(visit.visitor, entry) == Visit::stop) {
            return Walk::stopped;
        }
        offset += length;
    }
    return Walk::next_buffer;
}

// The flags of the open.  The close on exec keeps the descriptor out of a
// program that a thread of the process starts before the close.
constexpr int kDirectoryFlags = kOpenReadOnly | kOpenDirectory | kOpenCloseOnExec;

// Reads the records of the open directory descriptor from its offset to the
// end of the directory, and gives each name to visit.
[[nodiscard]] core::Result<core::Unit, Errno> walk(int descriptor, EntryVisit visit) noexcept {
    alignas(8) char records[kRecordBytes]{};
    bool is_done = false;
    bool has_failed = false;
    Errno failure = Errno::eio;
    while (!is_done && !has_failed) {
        result_of(raw_syscall(Syscall::getdents64, descriptor, static_cast<void const*>(records), kRecordBytes))
            .match(
                [&](std::uint64_t written) {
                    if (written == 0) {
                        is_done = true;
                        return;
                    }
                    switch (visit_records(records, static_cast<std::size_t>(written), visit)) {
                        case Walk::next_buffer:
                            break;
                        case Walk::stopped:
                            is_done = true;
                            break;
                        case Walk::malformed:
                        default:
                            has_failed = true;
                            break;
                    }
                },
                [&](Errno error) {
                    if (error != Errno::eintr) {
                        failure = error;
                        has_failed = true;
                    }
                });
    }
    if (has_failed) {
        return core::err(failure);
    }
    return core::Unit{};
}

// Gives each name of the open directory descriptor to visit, and closes the
// descriptor.  The close of a directory that a read opened has no error that
// the caller can act on, so the door does not report one.
[[nodiscard]] core::Result<core::Unit, Errno> walk_and_close(int descriptor, EntryVisit visit) noexcept {
    core::Result<core::Unit, Errno> walked = walk(descriptor, visit);
    static_cast<void>(raw_syscall(Syscall::close, descriptor));
    return walked;
}

}  // namespace

core::Result<core::Unit, Errno> list_directory_(core::CText path, EntryVisit visit) noexcept {
    char const* const first = core::detail::c_text_first_(path);
    return result_of_restarted(
               [first] { return raw_syscall(Syscall::openat, kAtCurrentDirectory, first, kDirectoryFlags); })
        .and_then(
            [visit](std::uint64_t descriptor) noexcept { return walk_and_close(static_cast<int>(descriptor), visit); });
}

core::Result<core::Unit, Errno> list_open_directory_(int descriptor, EntryVisit visit) noexcept {
    if (descriptor < 0) {
        return core::err(Errno::ebadf);
    }
    return walk(descriptor, visit);
}

}  // namespace foundation::os::detail
