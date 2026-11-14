// The file surface, run against a real directory.
//
// The negative-compile fixtures of fixy/os/Fs.h say what the gates refuse
// and nothing about what the mints do.  This file runs the mints.
//
// Everything runs in a scratch directory under /tmp (scratch_dir.h), which
// the test removes on the way out.  What the kernel can refuse — an
// unwritable /tmp — is skipped rather than failed.

#include <fixy/Core.h>
#include <foundation/core/Array.h>
#include <fixy/os/OwnedFile.h>
#include <fixy/os/Path.h>
#include <fixy/os/Fs.h>

#include "../scratch_dir.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include "../test_assert.h"

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {

using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

// Every path the surface takes is Path<Sanitized>, and the only way to
// one is through the sanitizer: tag the text External, then launder it.
[[nodiscard]] fixy::Path<src::Sanitized> sanitized(const std::string& raw) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(fixy::mint_foreign_text(raw.data(), raw.size())))
        .expect("each test path passes the sanitizer");
}

using crucible::test::ScratchDir;

// The descriptor of a mint, or an empty handle and the error number of the
// open.  The match gives the linear handle to its consumer on each path.
struct Opened final {
    fs::OwnedFd file;
    int error_number = 0;
};

[[nodiscard]] Opened opened_of(fixy::Result<fixy::Linear<fs::OwnedFd>, foundation::os::Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<fs::OwnedFd> opened) -> Opened { return Opened{std::move(opened).consume(), 0}; },
        [](foundation::os::Errno error) -> Opened { return Opened{fs::OwnedFd{}, std::to_underlying(error)}; });
}

// The error number of a result, or zero when it holds a value.
template <typename T>
[[nodiscard]] int errno_of(fixy::Result<T, foundation::os::Errno> const& result) {
    return std::to_underlying(result.err().value_or(foundation::os::Errno{}));
}

inline constexpr std::size_t kPayloadBytes = 4096;

void fill_pattern(std::uint8_t* out, std::uint8_t salt) {
    for (std::size_t index = 0; index < kPayloadBytes; ++index) {
        out[index] = static_cast<std::uint8_t>((index * 7u + salt) & 0xFFu);
    }
}

[[nodiscard]] bool write_all(int fd, const std::uint8_t* bytes) {
    std::size_t done = 0;
    while (done < kPayloadBytes) {
        const ::ssize_t n = ::write(fd, bytes + done, kPayloadBytes - done);
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// Reads a file back through OwnedFile, the stdio handle, so the two
// handle families meet at the byte level.  The handle comes through
// mint_owned_file, the mint that does the fopen under a context that owns
// IO and Block.  A value means the stream is open.
[[nodiscard]] bool file_holds(const std::string& path, const std::uint8_t* expected) {
    IoBlockCtx const ctx{eff::testing::test()};
    auto in = fixy::mint_owned_file(ctx, path.c_str(), "rb");
    if (!in) return false;
    std::uint8_t readback[kPayloadBytes];
    if (std::fread(readback, 1, kPayloadBytes, in->get()) != kPayloadBytes) return false;
    return std::memcmp(readback, expected, kPayloadBytes) == 0;
}

[[nodiscard]] int mint_file_writes_syncs_and_commits(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    std::uint8_t pattern[kPayloadBytes];
    fill_pattern(pattern, 1);

    const std::string tmp = scratch.file("plain.tmp");
    const std::string target = scratch.file("plain");

    Opened opened = opened_of(fs::mint_file<fixy::atom::fs::mode<fs::open_mode::WriteCreate>>(ctx, sanitized(tmp)));
    if (opened.error_number != 0) {
        std::fprintf(stderr, "mint_file WriteCreate failed (errno %d)\n", opened.error_number);
        return 1;
    }
    if (!opened.file.is_open()) {
        std::fprintf(stderr, "a successful mint handed back a closed descriptor\n");
        return 1;
    }
    if (!write_all(opened.file.get(), pattern)) {
        std::fprintf(stderr, "writing through the minted descriptor failed (errno %d)\n", errno);
        return 1;
    }
    if (auto synced = fs::sync<fs::sync_op::Fdatasync>(ctx, opened.file); synced.is_err()) {
        std::fprintf(stderr, "fdatasync failed (errno %d)\n", errno_of(synced));
        return 1;
    }
    // The empty handle closes the descriptor, and the commit renames the
    // closed file into place.
    opened.file = fs::OwnedFd{};

    if (auto committed = fs::commit_atomic<fs::atomicity::Rename>(ctx, sanitized(tmp), sanitized(target));
        committed.is_err()) {
        std::fprintf(stderr, "commit_atomic<Rename> failed (errno %d)\n", errno_of(committed));
        return 1;
    }
    if (!file_holds(target, pattern)) {
        std::fprintf(stderr, "the committed file does not hold the bytes that were written\n");
        return 1;
    }
    if (::access(tmp.c_str(), F_OK) == 0) {
        std::fprintf(stderr, "the temporary name still exists after the commit\n");
        return 1;
    }
    return 0;
}

// The directory descriptor is its own type, opened through the one
// door, and fsync on it is the call that flushes an entry.
[[nodiscard]] int dirfd_opens_and_flushes_the_entry(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};

    auto opened_dir = fs::open_dirfd(ctx, sanitized(scratch.path()));
    if (opened_dir.is_err()) {
        std::fprintf(stderr, "open_dirfd on the scratch directory failed (errno %d)\n", errno_of(opened_dir));
        return 1;
    }
    fs::Dirfd const dir = std::move(opened_dir).expect("the scratch directory opened");
    if (!dir.is_open()) {
        std::fprintf(stderr, "a successful open_dirfd handed back a closed handle\n");
        return 1;
    }
    if (auto flushed = fs::sync<fs::sync_op::FsyncParentDir>(ctx, dir); flushed.is_err()) {
        std::fprintf(stderr, "fsync on the directory descriptor failed (errno %d)\n", errno_of(flushed));
        return 1;
    }

    // A regular file is refused by O_DIRECTORY, so the door only opens
    // onto what the type claims.
    auto not_a_dir = fs::open_dirfd(ctx, sanitized(scratch.file("plain")));
    if (not_a_dir.is_ok()) {
        std::fprintf(stderr, "open_dirfd opened a regular file as a directory\n");
        return 1;
    }
    if (errno_of(not_a_dir) != ENOTDIR) {
        std::fprintf(stderr, "open_dirfd on a file failed with errno %d rather than ENOTDIR\n", errno_of(not_a_dir));
        return 1;
    }
    return 0;
}

// write_full puts every byte of the span, file_size reports them, and
// read_full returns them.  A span longer than the file stops at the end
// of the file, and an empty handle is refused with EBADF.
[[nodiscard]] int full_reads_and_writes_round_trip(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    std::uint8_t pattern[kPayloadBytes];
    fill_pattern(pattern, 4);
    const std::string path = scratch.file("full");

    Opened writer = opened_of(fs::mint_file<fixy::atom::fs::mode<fs::open_mode::WriteTruncate>>(ctx, sanitized(path)));
    if (writer.error_number != 0) {
        std::fprintf(stderr, "mint_file WriteTruncate failed (errno %d)\n", writer.error_number);
        return 1;
    }
    if (auto written = fs::write_full(ctx, writer.file, std::as_bytes(std::span{pattern})); written.is_err()) {
        std::fprintf(stderr, "write_full failed (errno %d)\n", errno_of(written));
        return 1;
    }
    if (fs::file_size(ctx, writer.file).value_or(-1) != static_cast<std::int64_t>(kPayloadBytes)) {
        std::fprintf(stderr, "file_size did not report the bytes that write_full put\n");
        return 1;
    }
    writer.file = fs::OwnedFd{};

    Opened const reader = opened_of(fs::mint_file<fixy::atom::fs::mode<fs::open_mode::ReadOnly>>(ctx, sanitized(path)));
    if (reader.error_number != 0) {
        std::fprintf(stderr, "mint_file ReadOnly failed (errno %d)\n", reader.error_number);
        return 1;
    }
    std::byte readback[kPayloadBytes * 2]{};
    if (fs::read_full(ctx, reader.file, std::span{readback}).value_or(0) != kPayloadBytes) {
        std::fprintf(stderr, "read_full did not stop at the end of the file\n");
        return 1;
    }
    if (std::memcmp(readback, pattern, kPayloadBytes) != 0) {
        std::fprintf(stderr, "read_full returned bytes that write_full did not put\n");
        return 1;
    }

    const fs::OwnedFd empty{};
    const auto refused = fs::read_full(ctx, empty, std::span{readback});
    if (refused.is_ok() || errno_of(refused) != EBADF) {
        std::fprintf(stderr, "read_full on an empty handle was not refused with EBADF\n");
        return 1;
    }
    const auto refused_write = fs::write_full(ctx, empty, std::as_bytes(std::span{pattern}));
    if (refused_write.is_ok() || errno_of(refused_write) != EBADF) {
        std::fprintf(stderr, "write_full on an empty handle was not refused with EBADF\n");
        return 1;
    }
    const auto refused_size = fs::file_size(ctx, empty);
    if (refused_size.is_ok() || errno_of(refused_size) != EBADF) {
        std::fprintf(stderr, "file_size on an empty handle was not refused with EBADF\n");
        return 1;
    }
    return 0;
}

// The calls under a directory: a new directory, its open, a file under it
// that a View and a TextView fill, a commit by a rename in the same
// directory, a whole read, a check of an entry and a remove.  The report of
// a failure goes through the Report family.
[[nodiscard]] int calls_under_a_directory(const ScratchDir& scratch) {
    namespace core = foundation::core;
    IoBlockCtx ctx{eff::testing::test()};
    eff::Alloc const alloc{};

    auto opened_root = fs::open_dirfd(ctx, sanitized(scratch.path()));
    if (opened_root.is_err()) {
        fixy::report(fixy::Sink::Err, "open_dirfd on the scratch directory failed (errno {})\n", errno_of(opened_root));
        return 1;
    }
    fs::Dirfd const root = std::move(opened_root).expect("the scratch directory opened");
    if (auto made = fs::make_directory_at(ctx, root, "objects"); made.is_err()) {
        fixy::report(fixy::Sink::Err, "make_directory_at failed (errno {})\n", errno_of(made));
        return 1;
    }
    if (auto again = fs::make_directory_at(ctx, root, "objects"); errno_of(again) != EEXIST) {
        fixy::report(fixy::Sink::Err, "a second make_directory_at did not give EEXIST\n");
        return 1;
    }
    auto opened_objects = fs::open_dirfd_at(ctx, root, "objects");
    if (opened_objects.is_err()) {
        fixy::report(fixy::Sink::Err, "open_dirfd_at failed (errno {})\n", errno_of(opened_objects));
        return 1;
    }
    fs::Dirfd const objects = std::move(opened_objects).expect("the objects directory opened");

    Opened writer =
        opened_of(fs::mint_file_at<fixy::atom::fs::mode<fs::open_mode::WriteTruncate>>(ctx, objects, "ab.tmp"));
    if (writer.error_number != 0) {
        fixy::report(fixy::Sink::Err, "mint_file_at WriteTruncate failed (errno {})\n", writer.error_number);
        return 1;
    }
    core::FixedArray<std::byte, 3> bytes{};
    core::fill(bytes.view(), std::byte{'x'});
    if (fs::write_full(ctx, writer.file, bytes.view()).is_err()
        || fs::write_full(ctx, writer.file, core::TextView{"yz"}).is_err()) {
        fixy::report(fixy::Sink::Err, "a write_full of a View or of a TextView failed\n");
        return 1;
    }
    if (fs::sync<fs::sync_op::Fdatasync>(ctx, writer.file).is_err()) {
        fixy::report(fixy::Sink::Err, "the fdatasync of the file under the directory failed\n");
        return 1;
    }
    writer.file = fs::OwnedFd{};
    if (auto committed = fs::commit_atomic_at<fs::atomicity::Rename>(ctx, objects, "ab.tmp", "ab");
        committed.is_err()) {
        fixy::report(fixy::Sink::Err, "commit_atomic_at<Rename> failed (errno {})\n", errno_of(committed));
        return 1;
    }
    if (fs::sync<fs::sync_op::FsyncParentDir>(ctx, objects).is_err()) {
        fixy::report(fixy::Sink::Err, "the fsync of the directory failed\n");
        return 1;
    }
    if (fs::has_entry_at(ctx, objects, "ab.tmp").value_or(true)
        || !fs::has_entry_at(ctx, objects, "ab").value_or(false)) {
        fixy::report(fixy::Sink::Err, "has_entry_at did not see the rename\n");
        return 1;
    }

    core::GrowText text = core::mint_grow_text(64);
    if (fs::read_whole_at(ctx, alloc, objects, "ab", text).value_or(0) != 5
        || !(text.view() == core::TextView{"xxxyz"})) {
        fixy::report(fixy::Sink::Err, "read_whole_at did not give the five bytes of the file\n");
        return 1;
    }
    core::GrowText small = core::mint_grow_text(4);
    if (errno_of(fs::read_whole_at(ctx, alloc, objects, "ab", small)) != EFBIG) {
        fixy::report(fixy::Sink::Err, "read_whole_at into a text that is too small did not give EFBIG\n");
        return 1;
    }
    if (fs::unlink_at(ctx, objects, "ab").is_err() || fs::has_entry_at(ctx, objects, "ab").value_or(true)) {
        fixy::report(fixy::Sink::Err, "unlink_at did not remove the file\n");
        return 1;
    }
    return 0;
}

// /proc/self is a symbolic link on each Linux host.  An open under a
// directory does not follow a symbolic link at the last part of its path,
// and gives ELOOP.  The link is an entry of its directory.
[[nodiscard]] int open_under_a_directory_refuses_a_symbolic_link() {
    IoBlockCtx ctx{eff::testing::test()};
    auto opened_proc = fs::open_dirfd(ctx, sanitized("/proc"));
    if (opened_proc.is_err()) {
        fixy::report(fixy::Sink::Err, "open_dirfd on /proc failed (errno {})\n", errno_of(opened_proc));
        return 1;
    }
    fs::Dirfd const proc = std::move(opened_proc).expect("/proc opened");
    if (opened_of(fs::mint_file_at<fs::read_only>(ctx, proc, "self")).error_number != ELOOP) {
        fixy::report(fixy::Sink::Err, "mint_file_at followed the symbolic link /proc/self\n");
        return 1;
    }
    if (!fs::has_entry_at(ctx, proc, "self").value_or(false)) {
        fixy::report(fixy::Sink::Err, "has_entry_at did not see the symbolic link /proc/self\n");
        return 1;
    }
    return 0;
}

// The entry check of a full path reads the path with no directory handle.
// It sees an entry that exists, sees no entry for a name that no directory
// holds, and gives the error of the kernel for a path that does not resolve.
[[nodiscard]] int has_entry_reads_a_full_path() {
    IoBlockCtx ctx{eff::testing::test()};
    if (!fs::has_entry(ctx, "/proc/self/status").value_or(false)) {
        fixy::report(fixy::Sink::Err, "has_entry did not see /proc/self/status\n");
        return 1;
    }
    if (fs::has_entry(ctx, "/proc/self/no-such-entry").value_or(true)) {
        fixy::report(fixy::Sink::Err, "has_entry saw an entry that does not exist\n");
        return 1;
    }
    if (fs::has_entry(ctx, "/proc/self/status/below-a-file").err().value_or(foundation::os::Errno{})
        != foundation::os::Errno::enotdir) {
        fixy::report(fixy::Sink::Err, "has_entry under a file did not give ENOTDIR\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    const ScratchDir scratch{"fixy-fs-test"};
    if (!scratch.is_ready()) {
        std::fprintf(stderr, "[skipped] cannot make a scratch directory under /tmp (errno %d)\n", errno);
        return 0;
    }
    if (const int rc = mint_file_writes_syncs_and_commits(scratch); rc != 0) return rc;
    if (const int rc = dirfd_opens_and_flushes_the_entry(scratch); rc != 0) return rc;
    if (const int rc = full_reads_and_writes_round_trip(scratch); rc != 0) return rc;
    if (const int rc = calls_under_a_directory(scratch); rc != 0) return rc;
    if (const int rc = open_under_a_directory_refuses_a_symbolic_link(); rc != 0) return rc;
    if (const int rc = has_entry_reads_a_full_path(); rc != 0) return rc;
    return 0;
}
