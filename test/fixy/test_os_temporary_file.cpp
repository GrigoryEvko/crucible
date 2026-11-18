// The temporary file door of fixy/os/Fs.h (THE TEMPORARY FILE):
// mint_temporary_file_at opens a regular file with no name under a
// directory, for reading and writing.
//
// The cases open such a file under /tmp, write bytes through the file door
// and read its size back.  Two files under one directory are two files.  The
// borrow of an empty directory handle gives ebadf, and a directory of a file
// system with no O_TMPFILE, such as /proc, gives an error and no descriptor.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
namespace src = ::fixy::tags::source;
using ::foundation::os::Errno;

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The file of a mint, or the Errno of the open that failed.
using Opened = fixy::Result<fs::TemporaryFile, Errno>;

[[nodiscard]] Opened descriptor_of(fixy::Result<fixy::Linear<fs::TemporaryFile>, Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<fs::TemporaryFile> opened) -> Opened { return std::move(opened).consume(); },
        [](Errno error) -> Opened { return fixy::err(error); });
}

// The size of a file, or -1 when the read of the size failed.
[[nodiscard]] std::int64_t size_of(IoBlockCtx const& ctx, fs::TemporaryFile const& file) {
    return fs::file_size(ctx, file.ref()).value_or(-1);
}

// A file under /tmp opens for a write, and its size is the count of the
// bytes that the write gave it.  A second file under the same directory has
// its own size.
[[nodiscard]] int check_temporary_file_takes_bytes() {
    IoBlockCtx const ctx{eff::testing::test()};
    fs::DirectoryHandle const temporary = fs::open_directory(ctx, sanitized("/tmp")).expect("/tmp opens");
    Opened first = descriptor_of(fs::mint_temporary_file_at(ctx, temporary.ref()));
    if (first.is_err()) {
        fixy::report(fixy::Sink::Err, "the temporary file under /tmp did not open (errno {})\n",
                     foundation::os::error_number_of(first));
        return 1;
    }
    fs::TemporaryFile const file = std::move(first).expect("the temporary file opened");
    if (size_of(ctx, file) != 0) return 2;
    if (fs::write_full(ctx, file.ref(), fixy::TextView{"twelve bytes"}).is_err()) return 3;
    if (size_of(ctx, file) != 12) return 4;
    Opened second = descriptor_of(fs::mint_temporary_file_at(ctx, temporary.ref(), fs::FileMode::owner_read));
    if (second.is_err()) return 5;
    fs::TemporaryFile const other = std::move(second).expect("the second temporary file opened");
    if (!other.is_open() || size_of(ctx, other) != 0) return 6;
    return 0;
}

// The borrow of an empty directory handle gives ebadf, and the door makes no
// system call.
[[nodiscard]] int check_empty_directory_gives_ebadf() {
    IoBlockCtx const ctx{eff::testing::test()};
    fs::DirectoryHandle emptied = fs::open_directory(ctx, sanitized("/tmp")).expect("/tmp opens");
    fs::DirectoryHandle const taken = std::move(emptied);
    Opened opened = descriptor_of(fs::mint_temporary_file_at(ctx, emptied.ref()));
    if (opened.err().value_or(Errno{}) != Errno::ebadf) return 11;
    if (!taken.is_open()) return 12;
    return 0;
}

// procfs makes no file, so the open under /proc gives an error and no
// descriptor.
[[nodiscard]] int check_procfs_refuses_the_file() {
    IoBlockCtx const ctx{eff::testing::test()};
    fs::DirectoryHandle const proc = fs::open_directory(ctx, sanitized("/proc")).expect("/proc opens");
    Opened opened = descriptor_of(fs::mint_temporary_file_at(ctx, proc.ref()));
    if (opened.is_ok()) return 21;
    return 0;
}

}  // namespace

int main() {
    int failed = check_temporary_file_takes_bytes();
    if (failed == 0) failed = check_empty_directory_gives_ebadf();
    if (failed == 0) failed = check_procfs_refuses_the_file();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_temporary_file: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_temporary_file: passed\n");
    return 0;
}
