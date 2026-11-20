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
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

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

// The file of a mint, or an empty handle and the error number of the open.
// The match gives the linear handle to its consumer on each path.
template <class File>
struct Opened final {
    File file;
    int error_number = 0;
};

template <class File>
[[nodiscard]] Opened<File> opened_of(fixy::Result<fixy::Linear<File>, foundation::os::Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<File> opened) -> Opened<File> { return Opened<File>{std::move(opened).consume(), 0}; },
        [](foundation::os::Errno error) -> Opened<File> { return Opened<File>{File{}, std::to_underlying(error)}; });
}

using ReadOnlyFile = fs::FileOf<fs::read_only>;
using WriteCreate = fixy::atom::fs::mode<fs::open_mode::WriteCreate>;
using WriteTruncate = fixy::atom::fs::mode<fs::open_mode::WriteTruncate>;

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

// Reads a file back through a second descriptor of the file door, opened
// for a read, so the write and the read meet at the byte level.
[[nodiscard]] bool file_holds(const std::string& path, const std::uint8_t (&expected)[kPayloadBytes]) {
    IoBlockCtx const ctx{eff::testing::test()};
    Opened<ReadOnlyFile> in = opened_of(fs::mint_file<fs::read_only>(ctx, sanitized(path)));
    if (in.error_number != 0) return false;
    fixy::FixedArray<std::byte, kPayloadBytes> readback{};
    if (fs::read_full(ctx, in.file.ref(), readback.view()).value_or(0) != kPayloadBytes) {
        return false;
    }
    return fixy::equal(readback.view(), foundation::core::bytes(expected));
}

[[nodiscard]] int mint_file_writes_syncs_and_commits(const ScratchDir& scratch) {
    IoBlockCtx ctx{eff::testing::test()};
    std::uint8_t pattern[kPayloadBytes];
    fill_pattern(pattern, 1);

    const std::string tmp = scratch.file("plain.tmp");
    const std::string target = scratch.file("plain");

    Opened<fs::FileOf<WriteCreate>> opened = opened_of(fs::mint_file<WriteCreate>(ctx, sanitized(tmp)));
    if (opened.error_number != 0) {
        std::fprintf(stderr, "mint_file WriteCreate failed (errno %d)\n", opened.error_number);
        return 1;
    }
    if (!opened.file.is_open()) {
        std::fprintf(stderr, "a successful mint handed back a closed descriptor\n");
        return 1;
    }
    if (auto written = fs::write_full(ctx, opened.file.ref(), foundation::core::bytes(pattern)); written.is_err()) {
        fixy::report(fixy::Sink::Err, "writing through the minted descriptor failed (errno {})\n", errno_of(written));
        return 1;
    }
    if (auto synced = fs::sync<fs::sync_op::Fdatasync>(ctx, opened.file.ref()); synced.is_err()) {
        std::fprintf(stderr, "fdatasync failed (errno %d)\n", errno_of(synced));
        return 1;
    }
    // The empty handle closes the descriptor, and the commit renames the
    // closed file into place.
    opened.file = fs::FileOf<WriteCreate>{};

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

    auto opened_dir = fs::open_directory(ctx, sanitized(scratch.path()));
    if (opened_dir.is_err()) {
        fixy::report(fixy::Sink::Err, "open_directory on the scratch directory failed (errno {})\n",
                     errno_of(opened_dir));
        return 1;
    }
    fs::DirectoryHandle const dir = std::move(opened_dir).expect("the scratch directory opened");
    if (!dir.is_open()) {
        fixy::report(fixy::Sink::Err, "a successful open_directory handed back a closed handle\n");
        return 1;
    }
    if (auto flushed = fs::sync<fs::sync_op::FsyncParentDir>(ctx, dir.ref()); flushed.is_err()) {
        fixy::report(fixy::Sink::Err, "fsync on the directory descriptor failed (errno {})\n", errno_of(flushed));
        return 1;
    }

    // A regular file is refused by O_DIRECTORY, so the door only opens
    // onto what the type claims.
    auto not_a_dir = fs::open_directory(ctx, sanitized(scratch.file("plain")));
    if (not_a_dir.is_ok()) {
        fixy::report(fixy::Sink::Err, "open_directory opened a regular file as a directory\n");
        return 1;
    }
    if (errno_of(not_a_dir) != ENOTDIR) {
        fixy::report(fixy::Sink::Err, "open_directory on a file failed with errno {} rather than ENOTDIR\n",
                     errno_of(not_a_dir));
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

    Opened<fs::FileOf<WriteTruncate>> writer = opened_of(fs::mint_file<WriteTruncate>(ctx, sanitized(path)));
    if (writer.error_number != 0) {
        std::fprintf(stderr, "mint_file WriteTruncate failed (errno %d)\n", writer.error_number);
        return 1;
    }
    if (auto written = fs::write_full(ctx, writer.file.ref(), foundation::core::bytes(pattern)); written.is_err()) {
        std::fprintf(stderr, "write_full failed (errno %d)\n", errno_of(written));
        return 1;
    }
    if (fs::file_size(ctx, writer.file.ref()).value_or(-1) != static_cast<std::int64_t>(kPayloadBytes)) {
        std::fprintf(stderr, "file_size did not report the bytes that write_full put\n");
        return 1;
    }
    writer.file = fs::FileOf<WriteTruncate>{};

    Opened<ReadOnlyFile> const reader = opened_of(fs::mint_file<fs::read_only>(ctx, sanitized(path)));
    if (reader.error_number != 0) {
        std::fprintf(stderr, "mint_file ReadOnly failed (errno %d)\n", reader.error_number);
        return 1;
    }
    fixy::FixedArray<std::byte, kPayloadBytes * 2> readback{};
    if (fs::read_full(ctx, reader.file.ref(), readback.view()).value_or(0) != kPayloadBytes) {
        std::fprintf(stderr, "read_full did not stop at the end of the file\n");
        return 1;
    }
    if (!fixy::equal(readback.view().window(0, kPayloadBytes).expect("the readback holds two payloads"),
                     foundation::core::bytes(pattern))) {
        std::fprintf(stderr, "read_full returned bytes that write_full did not put\n");
        return 1;
    }

    const fs::TemporaryFile empty{};
    const auto refused = fs::read_full(ctx, empty.ref(), readback.view());
    if (refused.is_ok() || errno_of(refused) != EBADF) {
        std::fprintf(stderr, "read_full on an empty handle was not refused with EBADF\n");
        return 1;
    }
    const auto refused_write = fs::write_full(ctx, empty.ref(), foundation::core::bytes(pattern));
    if (refused_write.is_ok() || errno_of(refused_write) != EBADF) {
        std::fprintf(stderr, "write_full on an empty handle was not refused with EBADF\n");
        return 1;
    }
    const auto refused_size = fs::file_size(ctx, empty.ref());
    if (refused_size.is_ok() || errno_of(refused_size) != EBADF) {
        std::fprintf(stderr, "file_size on an empty handle was not refused with EBADF\n");
        return 1;
    }
    return 0;
}

// read_full_at reads a part of a file at its offset and leaves the offset of
// the descriptor where it was.  A read past the end gives the bytes before
// the end.  set_file_size cuts a file through the handle of an append open,
// the next append writes at the new end, and a larger size adds zero bytes.
// A negative offset or size gives EINVAL, and an empty handle gives EBADF.
[[nodiscard]] int reads_at_an_offset_and_sets_the_size(const ScratchDir& scratch) {
    namespace core = foundation::core;
    using WriteAppend = fixy::atom::fs::mode<fs::open_mode::WriteAppend>;
    IoBlockCtx ctx{eff::testing::test()};
    auto opened_root = fs::open_directory(ctx, sanitized(scratch.path()));
    if (opened_root.is_err()) {
        fixy::report(fixy::Sink::Err, "open_directory on the scratch directory failed (errno {})\n",
                     errno_of(opened_root));
        return 1;
    }
    fs::DirectoryHandle const root_handle = std::move(opened_root).expect("the scratch directory opened");
    fs::DirectoryRef<fs::directory_rights> const root = root_handle.ref();
    {
        Opened<fs::FileOf<WriteTruncate>> writer = opened_of(fs::mint_file_at<WriteTruncate>(ctx, root, "positioned"));
        if (writer.error_number != 0 || fs::write_full(ctx, writer.file.ref(), core::TextView{"0123456789"}).is_err()) {
            fixy::report(fixy::Sink::Err, "the test does not write the file of the positioned reads\n");
            return 1;
        }
    }
    Opened<ReadOnlyFile> const reader = opened_of(fs::mint_file_at<fs::read_only>(ctx, root, "positioned"));
    if (reader.error_number != 0) {
        fixy::report(fixy::Sink::Err, "mint_file ReadOnly of the positioned file failed (errno {})\n",
                     reader.error_number);
        return 1;
    }
    // True when the first bytes of read are the characters of expected.
    const auto holds_text = [](core::View<std::byte const> read, core::TextView expected) {
        bool is_same = expected.size() <= read.size();
        std::size_t index = 0;
        for (const char character : expected) {
            is_same = is_same && read.get(index).is_some_and([character](auto const& byte) noexcept {
                return *byte == static_cast<std::byte>(character);
            });
            ++index;
        }
        return is_same;
    };
    core::FixedArray<std::byte, 4> part{};
    const auto part_holds = [&part, &holds_text](core::TextView expected, std::size_t count) {
        return count == expected.size() && holds_text(part.view(), expected);
    };
    const auto middle = fs::read_full_at(ctx, reader.file.ref(), part.view(), fs::FileOffset{3});
    if (middle.err().is_some() || !part_holds(core::TextView{"3456"}, 4)
        || fs::read_full_at(ctx, reader.file.ref(), part.view(), fs::FileOffset{3}).value_or(0) != 4) {
        fixy::report(fixy::Sink::Err, "read_full_at at offset 3 did not give the bytes 3456\n");
        return 1;
    }
    core::fill(part.view(), std::byte{0});
    if (fs::read_full_at(ctx, reader.file.ref(), part.view(), fs::FileOffset{8}).value_or(0) != 2
        || !part_holds(core::TextView{"89"}, 2)) {
        fixy::report(fixy::Sink::Err, "read_full_at at offset 8 did not stop at the end of the file\n");
        return 1;
    }
    if (fs::read_full_at(ctx, reader.file.ref(), part.view(), fs::FileOffset{20}).value_or(1) != 0) {
        fixy::report(fixy::Sink::Err, "read_full_at past the end of the file gave bytes\n");
        return 1;
    }
    if (errno_of(fs::read_full_at(ctx, reader.file.ref(), part.view(), fs::FileOffset{-1})) != EINVAL) {
        fixy::report(fixy::Sink::Err, "read_full_at at a negative offset did not give EINVAL\n");
        return 1;
    }
    // The reads at an offset left the offset of the descriptor at zero.
    if (fs::read_full(ctx, reader.file.ref(), part.view()).value_or(0) != 4 || !part_holds(core::TextView{"0123"}, 4)) {
        fixy::report(fixy::Sink::Err, "read_full_at moved the offset of the descriptor\n");
        return 1;
    }

    Opened<fs::FileOf<WriteAppend>> appender = opened_of(fs::mint_file_at<WriteAppend>(ctx, root, "positioned"));
    if (appender.error_number != 0) {
        fixy::report(fixy::Sink::Err, "mint_file WriteAppend failed (errno {})\n", appender.error_number);
        return 1;
    }
    if (fs::set_file_size(ctx, appender.file.ref(), fs::FileOffset{5}).is_err()
        || fs::file_size(ctx, appender.file.ref()).value_or(-1) != 5
        || fs::write_full(ctx, appender.file.ref(), core::TextView{"ab"}).is_err()) {
        fixy::report(fixy::Sink::Err, "set_file_size did not cut the file of the append open to 5 bytes\n");
        return 1;
    }
    core::FixedArray<std::byte, 16> whole{};
    if (fs::read_full_at(ctx, reader.file.ref(), whole.view(), fs::FileOffset{0}).value_or(0) != 7
        || !holds_text(whole.view(), core::TextView{"01234ab"})) {
        fixy::report(fixy::Sink::Err, "the append after the cut did not write at the new end\n");
        return 1;
    }
    if (fs::set_file_size(ctx, appender.file.ref(), fs::FileOffset{9}).is_err()
        || fs::read_full_at(ctx, reader.file.ref(), whole.view(), fs::FileOffset{0}).value_or(0) != 9
        || *whole.get(7).expect("the buffer holds nine bytes") != std::byte{0}
        || *whole.get(8).expect("the buffer holds nine bytes") != std::byte{0}) {
        fixy::report(fixy::Sink::Err, "set_file_size to a larger size did not add zero bytes\n");
        return 1;
    }
    if (errno_of(fs::set_file_size(ctx, appender.file.ref(), fs::FileOffset{-1})) != EINVAL) {
        fixy::report(fixy::Sink::Err, "set_file_size to a negative size did not give EINVAL\n");
        return 1;
    }

    const fs::TemporaryFile empty{};
    if (errno_of(fs::read_full_at(ctx, empty.ref(), part.view(), fs::FileOffset{0})) != EBADF
        || errno_of(fs::set_file_size(ctx, empty.ref(), fs::FileOffset{0})) != EBADF) {
        fixy::report(fixy::Sink::Err, "a door at an offset on an empty handle was not refused with EBADF\n");
        return 1;
    }
    return 0;
}

// An exclusive open makes a new file.  A second exclusive open of the same
// name gives EEXIST and leaves the bytes of the first file, so two writers
// never share one file.  After the remove of the name, an exclusive open
// makes a new file again.
[[nodiscard]] int exclusive_open_makes_a_new_file(const ScratchDir& scratch) {
    namespace core = foundation::core;
    using WriteExclusive = fixy::atom::fs::mode<fs::open_mode::WriteExclusive>;
    IoBlockCtx ctx{eff::testing::test()};
    auto opened_root = fs::open_directory(ctx, sanitized(scratch.path()));
    if (opened_root.is_err()) {
        fixy::report(fixy::Sink::Err, "open_directory on the scratch directory failed (errno {})\n",
                     errno_of(opened_root));
        return 1;
    }
    fs::DirectoryHandle const root_handle = std::move(opened_root).expect("the scratch directory opened");
    fs::DirectoryRef<fs::directory_rights> const root = root_handle.ref();
    Opened<fs::FileOf<WriteExclusive>> first = opened_of(fs::mint_file_at<WriteExclusive>(ctx, root, "exclusive"));
    if (first.error_number != 0 || fs::write_full(ctx, first.file.ref(), core::TextView{"abc"}).is_err()) {
        fixy::report(fixy::Sink::Err, "the first exclusive open did not make and write a file (errno {})\n",
                     first.error_number);
        return 1;
    }
    Opened<fs::FileOf<WriteExclusive>> second = opened_of(fs::mint_file_at<WriteExclusive>(ctx, root, "exclusive"));
    if (second.error_number != EEXIST || second.file.is_open()) {
        fixy::report(fixy::Sink::Err, "a second exclusive open of one name gave errno {} and not EEXIST\n",
                     second.error_number);
        return 1;
    }
    Opened<ReadOnlyFile> const reader = opened_of(fs::mint_file_at<fs::read_only>(ctx, root, "exclusive"));
    if (reader.error_number != 0 || fs::file_size(ctx, reader.file.ref()).value_or(-1) != 3) {
        fixy::report(fixy::Sink::Err, "the refused exclusive open changed the file of the first open\n");
        return 1;
    }
    if (fs::unlink_at(ctx, root, "exclusive").is_err()
        || opened_of(fs::mint_file_at<WriteExclusive>(ctx, root, "exclusive")).error_number != 0) {
        fixy::report(fixy::Sink::Err, "an exclusive open after the remove of the name did not make a file\n");
        return 1;
    }
    return 0;
}

// The loops of the full transfers end when a step moves zero bytes.  Each
// step below moves the count of its plan for its call, and gives enospc after
// kStepLimit calls.  So a loop that does not stop on a zero ends with enospc
// and not with the result of the case.  A full write that the kernel takes
// none of ends with eio, and a full read that gets zero bytes ends at the end
// of the file.
[[nodiscard]] int full_transfers_end_on_a_zero_step() {
    namespace core = foundation::core;
    using foundation::os::Errno;
    using Step = core::Result<std::uint64_t, Errno>;
    constexpr std::size_t kStepLimit = 64;
    constexpr std::size_t kBytes = 5;

    // The step of a plan: first_count on the first call, later_count on each
    // other call, eintr on the call that interrupted_call names, and enospc
    // after the limit.  calls counts the calls.
    std::size_t calls = 0;
    auto const planned_step = [&calls](std::uint64_t first_count, std::uint64_t later_count,
                                       std::size_t interrupted_call) {
        return [&calls, first_count, later_count, interrupted_call](std::size_t, std::size_t) -> Step {
            calls += 1;
            if (calls > kStepLimit) return core::err(Errno::enospc);
            if (calls == interrupted_call) return core::err(Errno::eintr);
            return calls == 1 ? first_count : later_count;
        };
    };

    auto const none_taken = fs::detail::write_full_steps_(kBytes, planned_step(0, 0, 0));
    if (none_taken.err().value_or(Errno{}) != Errno::eio || calls != 1) {
        fixy::report(fixy::Sink::Err, "a full write that the kernel took none of ended with errno {} after {} steps\n",
                     errno_of(none_taken), calls);
        return 1;
    }
    calls = 0;
    auto const part_taken = fs::detail::write_full_steps_(kBytes, planned_step(3, 0, 0));
    if (part_taken.err().value_or(Errno{}) != Errno::eio || calls != 2) {
        fixy::report(fixy::Sink::Err, "a full write with a short first take ended with errno {} after {} steps\n",
                     errno_of(part_taken), calls);
        return 1;
    }
    calls = 0;
    auto const interrupted = fs::detail::write_full_steps_(kBytes, planned_step(0, kBytes, 1));
    if (interrupted.is_err() || calls != 2) {
        fixy::report(fixy::Sink::Err, "a full write after a signal ended with errno {} after {} steps\n",
                     errno_of(interrupted), calls);
        return 1;
    }
    calls = 0;
    auto empty_file = fs::detail::read_full_steps_(kBytes, planned_step(0, 0, 0));
    if (std::move(empty_file).value_or(kBytes) != 0 || calls != 1) {
        fixy::report(fixy::Sink::Err, "a full read of an empty file did not end at its first step\n");
        return 1;
    }
    calls = 0;
    auto short_file = fs::detail::read_full_steps_(kBytes, planned_step(3, 0, 0));
    if (std::move(short_file).value_or(0) != 3 || calls != 2) {
        fixy::report(fixy::Sink::Err, "a full read of a file of three bytes did not end at its end\n");
        return 1;
    }
    return 0;
}

// The open beneath a directory tries again after eagain, which a rename that
// races the lookup of a ".." part gives, and it stops after kBeneathTries
// steps that gave it.  A signal adds a step and no try.  Each other error
// ends the open at once.
[[nodiscard]] int open_beneath_ends_at_the_bound_of_its_tries() {
    namespace core = foundation::core;
    using foundation::os::Errno;
    using Step = core::Result<std::uint64_t, Errno>;
    constexpr std::size_t kTries = fs::detail::kBeneathTries;

    // The step of a plan: eintr on the call that interrupted_call names,
    // eagain on the first raced other calls, and descriptor 7 after them.
    // calls counts the calls.
    std::size_t calls = 0;
    auto const planned_step = [&calls](std::size_t raced, std::size_t interrupted_call) {
        return [&calls, raced, interrupted_call, given = std::size_t{0}]() mutable -> Step {
            calls += 1;
            if (calls == interrupted_call) return core::err(Errno::eintr);
            if (given < raced) {
                given += 1;
                return core::err(Errno::eagain);
            }
            return std::uint64_t{7};
        };
    };

    auto const opened_late = fs::detail::open_beneath_steps_(planned_step(kTries - 1, 0));
    if (opened_late.is_err() || calls != kTries) {
        fixy::report(fixy::Sink::Err, "an open after {} eagain steps ended with errno {} after {} steps\n", kTries - 1,
                     errno_of(opened_late), calls);
        return 1;
    }
    calls = 0;
    auto const raced_out = fs::detail::open_beneath_steps_(planned_step(kTries, 0));
    if (raced_out.err().value_or(Errno{}) != Errno::eagain || calls != kTries) {
        fixy::report(fixy::Sink::Err, "an open after {} eagain steps ended with errno {} after {} steps\n", kTries,
                     errno_of(raced_out), calls);
        return 1;
    }
    calls = 0;
    auto const interrupted = fs::detail::open_beneath_steps_(planned_step(kTries - 1, 1));
    if (interrupted.is_err() || calls != kTries + 1) {
        fixy::report(fixy::Sink::Err, "an open after a signal and {} eagain steps ended with errno {} after {} steps\n",
                     kTries - 1, errno_of(interrupted), calls);
        return 1;
    }
    calls = 0;
    auto const refused = fs::detail::open_beneath_steps_([&calls]() -> Step {
        calls += 1;
        return core::err(Errno::exdev);
    });
    if (refused.err().value_or(Errno{}) != Errno::exdev || calls != 1) {
        fixy::report(fixy::Sink::Err, "an open that the kernel refused ended with errno {} after {} steps\n",
                     errno_of(refused), calls);
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
    eff::Alloc const alloc = eff::testing::test().alloc;

    auto opened_root = fs::open_directory(ctx, sanitized(scratch.path()));
    if (opened_root.is_err()) {
        fixy::report(fixy::Sink::Err, "open_directory on the scratch directory failed (errno {})\n",
                     errno_of(opened_root));
        return 1;
    }
    fs::DirectoryHandle const root_handle = std::move(opened_root).expect("the scratch directory opened");
    fs::DirectoryRef<fs::directory_rights> const root = root_handle.ref();
    if (auto made = fs::make_directory_at(ctx, root, "objects"); made.is_err()) {
        fixy::report(fixy::Sink::Err, "make_directory_at failed (errno {})\n", errno_of(made));
        return 1;
    }
    if (auto again = fs::make_directory_at(ctx, root, "objects"); errno_of(again) != EEXIST) {
        fixy::report(fixy::Sink::Err, "a second make_directory_at did not give EEXIST\n");
        return 1;
    }
    auto opened_objects = fs::open_directory_at(ctx, root, "objects");
    if (opened_objects.is_err()) {
        fixy::report(fixy::Sink::Err, "open_directory_at failed (errno {})\n", errno_of(opened_objects));
        return 1;
    }
    fs::DirectoryHandle const objects_handle = std::move(opened_objects).expect("the objects directory opened");
    fs::DirectoryRef<fs::directory_rights> const objects = objects_handle.ref();

    Opened<fs::FileOf<WriteTruncate>> writer = opened_of(fs::mint_file_at<WriteTruncate>(ctx, objects, "ab.tmp"));
    if (writer.error_number != 0) {
        fixy::report(fixy::Sink::Err, "mint_file_at WriteTruncate failed (errno {})\n", writer.error_number);
        return 1;
    }
    core::FixedArray<std::byte, 3> bytes{};
    core::fill(bytes.view(), std::byte{'x'});
    if (fs::write_full(ctx, writer.file.ref(), bytes.view()).is_err()
        || fs::write_full(ctx, writer.file.ref(), core::TextView{"yz"}).is_err()) {
        fixy::report(fixy::Sink::Err, "a write_full of a View or of a TextView failed\n");
        return 1;
    }
    if (fs::sync<fs::sync_op::Fdatasync>(ctx, writer.file.ref()).is_err()) {
        fixy::report(fixy::Sink::Err, "the fdatasync of the file under the directory failed\n");
        return 1;
    }
    writer.file = fs::FileOf<WriteTruncate>{};
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
        || !text.with_view([](core::TextView read) noexcept { return read == core::TextView{"xxxyz"}; })) {
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

    // A path with a directory part: each door opens the part beneath root,
    // and acts on the final name in it.  The two names of the commit have two
    // directory parts, and a ".." part that stays inside root is permitted.
    if (fs::make_directory_at(ctx, root, "objects/inner/").is_err()
        || !fs::has_entry_at(ctx, root, "objects/inner").value_or(false)) {
        fixy::report(fixy::Sink::Err, "make_directory_at of objects/inner/ did not make the directory\n");
        return 1;
    }
    Opened<fs::FileOf<WriteCreate>> nested = opened_of(fs::mint_file_at<WriteCreate>(ctx, root, "objects/moved.tmp"));
    if (nested.error_number != 0) {
        fixy::report(fixy::Sink::Err, "mint_file_at of objects/moved.tmp failed (errno {})\n", nested.error_number);
        return 1;
    }
    nested.file = fs::FileOf<WriteCreate>{};
    if (fs::commit_atomic_at<fs::atomicity::RenameAt2NoReplace>(ctx, root, "objects/moved.tmp", "objects/inner/moved")
            .is_err()
        || !fs::has_entry_at(ctx, objects, "inner/moved").value_or(false)
        || fs::unlink_at(ctx, root, "objects/inner/../inner/moved").is_err()
        || fs::has_entry_at(ctx, objects, "inner/moved").value_or(true)) {
        fixy::report(fixy::Sink::Err, "a commit or an unlink under a directory part did not act on its entry\n");
        return 1;
    }
    return 0;
}

// /proc/self is a symbolic link on each Linux host.  An open under a
// directory does not follow a symbolic link at the last part of its path,
// and gives ELOOP.  The link is an entry of its directory.
[[nodiscard]] int open_under_a_directory_refuses_a_symbolic_link() {
    IoBlockCtx ctx{eff::testing::test()};
    auto opened_proc = fs::open_directory(ctx, sanitized("/proc"));
    if (opened_proc.is_err()) {
        fixy::report(fixy::Sink::Err, "open_directory on /proc failed (errno {})\n", errno_of(opened_proc));
        return 1;
    }
    fs::DirectoryHandle const proc_handle = std::move(opened_proc).expect("/proc opened");
    fs::DirectoryRef<fs::directory_rights> const proc = proc_handle.ref();
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
    if (const int rc = reads_at_an_offset_and_sets_the_size(scratch); rc != 0) return rc;
    if (const int rc = exclusive_open_makes_a_new_file(scratch); rc != 0) return rc;
    if (const int rc = full_transfers_end_on_a_zero_step(); rc != 0) return rc;
    if (const int rc = open_beneath_ends_at_the_bound_of_its_tries(); rc != 0) return rc;
    if (const int rc = calls_under_a_directory(scratch); rc != 0) return rc;
    if (const int rc = open_under_a_directory_refuses_a_symbolic_link(); rc != 0) return rc;
    if (const int rc = has_entry_reads_a_full_path(); rc != 0) return rc;
    return 0;
}
