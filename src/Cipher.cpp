// The members of crucible/Cipher.h that read or write files, and the members
// that encode a region or a session-event batch.  The header keeps the
// members that a context parameterizes.

#include <crucible/Cipher.h>

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <crucible/cipher/CipherTierPromotion.h>
#include <crucible/cipher/FederationProtocol.h>
#include <fixy/os/Fs.h>
#include <foundation/os/PathText.h>
#include <foundation/reflect/Mix.h>

#include <cstring>
#include <span>
#include <vector>

namespace crucible {

namespace {

// The path of an entry under the root of the store, with no leading slash,
// because each file call reads it relative to the descriptor of the root.
// The longest is the directory of the events of a session: "session_events/"
// (15) and 16 digits, 31 characters.  The longest temporary name is the one
// of a batch in its session directory: 16 digits, ".cfed" (5), ".tmp-" (5)
// and 2 digits, 28 characters.
using EntryPath = ::foundation::os::PathText<64>;

// The name of the shard directory of the object of hash under objects: the
// first two hexadecimal digits of the hash.
[[nodiscard]] EntryPath object_shard_(std::uint64_t hash) noexcept {
    EntryPath name{};
    name.append_hex<2>(hash >> 56U).expect("the shard name fits its path");
    return name;
}

// The name of the object of hash in its shard directory: the last fourteen
// hexadecimal digits of the hash.
[[nodiscard]] EntryPath object_leaf_(std::uint64_t hash) noexcept {
    EntryPath name{};
    name.append_hex<14>(hash).expect("the object name fits its path");
    return name;
}

// The entry of the object of hash: "objects/<2 digits>/<14 digits>", the
// sixteen hexadecimal digits of the hash.
[[nodiscard]] EntryPath object_entry_(std::uint64_t hash) noexcept {
    const EntryPath shard = object_shard_(hash);
    const EntryPath leaf = object_leaf_(hash);
    EntryPath path{};
    path.append(::fixy::TextView{"objects/"}).expect("the object entry fits its path");
    path.append(shard.c_text().view()).expect("the object entry fits its path");
    path.append('/').expect("the object entry fits its path");
    path.append(leaf.c_text().view()).expect("the object entry fits its path");
    return path;
}

// The name of the directory of the events of a session under
// session_events: the sixteen hexadecimal digits of the session.
[[nodiscard]] EntryPath session_name_(::fixy::session::SessionTagId session) noexcept {
    EntryPath name{};
    name.append_hex<16>(session.value).expect("the session name fits its path");
    return name;
}

// The entry of the directory of the events of a session:
// "session_events/<16 digits>".
[[nodiscard]] EntryPath session_entry_(::fixy::session::SessionTagId session) noexcept {
    const EntryPath name = session_name_(session);
    EntryPath path{};
    path.append(::fixy::TextView{"session_events/"}).expect("the session entry fits its path");
    path.append(name.c_text().view()).expect("the session entry fits its path");
    return path;
}

// The sixteen hexadecimal digits of value, in lower case.
[[nodiscard]] ::fixy::FixedText<16> hex_digits_(std::uint64_t value) noexcept {
    ::fixy::FixedText<16> digits{};
    ::fixy::append_hex<16>(digits, value).expect("sixteen digits fit sixteen characters");
    return digits;
}

// Gives the result of call for the context of the caller.  The context is
// one of three types, so the call compiles one time for each capability.  It
// does not throw when no form of call throws.
template <class Call>
[[nodiscard]] auto with_file_ctx_(Cipher::FileCtx const& file_ctx,
                                  Call const& call) noexcept(noexcept(file_ctx.match(call, call, call))) {
    return file_ctx.match(call, call, call);
}

// Opens path under dir read-only and flushes its data.
//
// Both the new-write path and the idempotent-skip path in store() flush
// through here, because skipping the flush breaks cross-process
// recovery:
//
//   Process A writes the file and its fdatasync fails with EIO.  A
//   returns the none hash so its caller treats the store as failed, but
//   the file on disk holds bytes whose writeback was never acknowledged.
//   A then exits.  Process B stores the same content hash, sees the file
//   exists, and would return the hash as durable without ever forcing
//   those bytes out.
//
// Routing the skip path through fdatasync either forces the flush or
// fails, in which case B reports failure exactly as A did.
template <class Ctx>
[[nodiscard]] bool flush_entry_at_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir,
                                   ::fixy::CText path) noexcept {
    using ReadFile = ::fixy::fs::FileOf<::fixy::fs::read_only>;
    return ::fixy::fs::mint_file_at<::fixy::fs::read_only>(ctx, dir.ref(), path)
        .match(
            [&ctx](::fixy::Linear<ReadFile> opened) {
                const ReadFile file = std::move(opened).consume();
                return ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file.ref()).is_ok();
            },
            [](::foundation::os::Errno) { return false; });
}

// How a write of an entry treats an entry that is at its name.
enum class EntryCommit : std::uint8_t {
    // The rename replaces the entry.  HEAD takes it, because its content
    // changes from commit to commit.
    replace,
    // The rename keeps the entry, and the write gives EntryWrite::exists.  An
    // object and a session-event batch take it, because their content names
    // them, so a sound entry must not change.
    keep_existing,
};

// The result of a write of an entry.
enum class EntryWrite : std::uint8_t {
    // The entry holds the bytes of the write, and they are durable.
    written,
    // Another writer made the entry first, and the write removed its own
    // temporary file.  The caller checks the entry that is at the name.
    exists,
    failed,
};

// The count of the temporary names <name>.tmp-<two digits> that a write
// tries.  A name that an entry holds belongs to another writer that writes
// the same entry, or to a writer that a crash ended, so the write tries the
// next name.
inline constexpr std::uint64_t kTemporaryNameTries = 64;

// Renames temporary onto target under dir and syncs dir.  keep_existing
// renames with RENAME_NOREPLACE: an entry at target gives exists.  A file
// system with no RENAME_NOREPLACE gives einval, and the rename then replaces
// the entry, so the last full file of the name wins.  Each failure removes
// the temporary file.
template <class Ctx>
[[nodiscard]] EntryWrite commit_temporary_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir,
                                           EntryPath const& temporary, EntryPath const& target,
                                           EntryCommit commit) noexcept {
    using ::foundation::os::Errno;
    const auto replace = [&] {
        return ::fixy::fs::commit_atomic_at<::fixy::fs::atomicity::Rename>(ctx, dir.ref(), temporary.c_text(),
                                                                           target.c_text());
    };
    const auto is_error = [](::fixy::Result<::fixy::Unit, Errno> const& renamed, Errno error) noexcept {
        return renamed.is_err_and([error](Errno const& given) noexcept { return given == error; });
    };
    ::fixy::Result<::fixy::Unit, Errno> renamed =
        commit == EntryCommit::replace ? replace()
                                       : ::fixy::fs::commit_atomic_at<::fixy::fs::atomicity::RenameAt2NoReplace>(
                                             ctx, dir.ref(), temporary.c_text(), target.c_text());
    if (is_error(renamed, Errno::einval)) renamed = replace();
    if (renamed.is_err()) {
        static_cast<void>(::fixy::fs::unlink_at(ctx, dir.ref(), temporary.c_text()).is_ok());
        return is_error(renamed, Errno::eexist) ? EntryWrite::exists : EntryWrite::failed;
    }
    // dir is owned by the caller and outlives this call, so the fsync needs
    // no re-open of the parent path.
    return ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir.ref()).is_ok() ? EntryWrite::written
                                                                                         : EntryWrite::failed;
}

// Atomic write of an entry: write the bytes to a temporary file of this
// writer under dir, fdatasync them, rename the file onto path, then fsync
// dir.
//
// A plain truncate-and-write is the alternative and it loses.  The file
// is observably empty between the truncate and the first write, and
// partial between successive writes, so a crash in either window leaves
// it corrupt.  Under the rename, the reader sees either the whole old
// content or the whole new content.
//
// Each writer has its own temporary file.  The open of a temporary name is
// exclusive (O_EXCL), so a name that another writer holds gives eexist, and
// the write tries the next name.  Two writers of one entry with one shared
// temporary name could truncate each other: one renames the file, and the
// other cuts it to nothing before its own write reaches the disk.  A crash
// before the rename leaves the temporary file of the crashed writer, and the
// next writer takes the next name.
//
// The parent dirfd fsync is not optional.  renameat returns before the
// directory entry necessarily reaches storage, and POSIX permits a crash
// in between.  The rename would be lost, the old content would stand,
// and the new bytes would be orphaned in the tmp file.  The fsync reaches
// the directory of the rename only when path is one name in dir, so a path
// with a slash ends the process.
//
// The doors retry EINTR on write, fdatasync, renameat and fsync.  If any
// step fails after the tmp file exists, it is unlinked so a later run does
// not trip over a partial-write artifact.  Bytes is a TextView, a span of
// bytes or a View of bytes, the buffers that write_full takes.  Complexity:
// one open for each temporary name that another writer holds, at most
// kTemporaryNameTries.
template <class Ctx, class Bytes>
[[nodiscard]] EntryWrite write_entry_at_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir, ::fixy::TextView path,
                                         Bytes bytes, EntryCommit commit) noexcept {
    CRUCIBLE_PRE(::fixy::find(path, '/').is_none());
    EntryPath target{};
    if (target.append(path).is_err()) return EntryWrite::failed;
    using WriteExclusive = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteExclusive>;
    using TemporaryFile = ::fixy::fs::FileOf<WriteExclusive>;
    for (std::uint64_t attempt = 0; attempt < kTemporaryNameTries; ++attempt) {
        EntryPath temporary{};
        const bool is_named = temporary.append(path).is_ok() && temporary.append(::fixy::TextView{".tmp-"}).is_ok()
                           && temporary.append_hex<2>(attempt).is_ok();
        if (!is_named) return EntryWrite::failed;
        // No value: another writer holds the name, and the write tries the
        // next one.
        const ::fixy::Option<EntryWrite> result =
            ::fixy::fs::mint_file_at<WriteExclusive>(ctx, dir.ref(), temporary.c_text())
                .match(
                    [&](::fixy::Linear<TemporaryFile> opened) -> ::fixy::Option<EntryWrite> {
                        // The nested scope closes the writer before the
                        // rename, which is what POSIX recommends for
                        // atomic-rename writes and keeps no extra descriptor
                        // open across the rename window.
                        bool is_durable = false;
                        {
                            const TemporaryFile file = std::move(opened).consume();
                            is_durable = ::fixy::fs::write_full(ctx, file.ref(), bytes).is_ok()
                                      && ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file.ref()).is_ok();
                        }
                        if (!is_durable) {
                            static_cast<void>(::fixy::fs::unlink_at(ctx, dir.ref(), temporary.c_text()).is_ok());
                            return ::fixy::some(EntryWrite::failed);
                        }
                        // Anchoring both sides of the rename at the same dirfd
                        // makes the swap immune to a concurrent rename of the
                        // parent directory.
                        return ::fixy::some(commit_temporary_(ctx, dir, temporary, target, commit));
                    },
                    [](::foundation::os::Errno error) -> ::fixy::Option<EntryWrite> {
                        if (error == ::foundation::os::Errno::eexist) return ::fixy::none;
                        return ::fixy::some(EntryWrite::failed);
                    });
        for (const EntryWrite written : result)
            return written;
    }
    return EntryWrite::failed;
}

// The bytes of one read of the scan of whole_lines_size_.  A torn record is
// at most one record of at most 96 bytes, so one read finds the last newline
// of each file that a writer of the store made.
inline constexpr std::size_t kTailChunkBytes = 256;

// The offset after the last newline of the first size bytes of file, zero
// when they hold no newline, or none when a read fails.  The scan reads the
// file back from size, one chunk at a time.  Complexity: one read for each
// chunk from size back to the last newline.
template <class Ctx, ::fixy::os::RightSet Rights>
[[nodiscard]] ::fixy::Option<::fixy::fs::FileOffset> whole_lines_size_(Ctx const& ctx, ::fixy::fs::FileRef<Rights> file,
                                                                       ::fixy::fs::FileOffset size) noexcept {
    auto [chunk, chunk_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::byte, kTailChunkBytes>);
    return chunk.with_mut(chunk_key, [&ctx, file, size](auto run) noexcept -> ::fixy::Option<::fixy::fs::FileOffset> {
        std::int64_t end = size.raw();
        while (end > 0) {
            const std::int64_t start = ::fixy::max(end - static_cast<std::int64_t>(kTailChunkBytes), std::int64_t{0});
            // The count is at most one chunk, so each prefix of the count is
            // the whole request.
            const std::size_t count = static_cast<std::size_t>(end - start);
            auto [read, chunk_run] = ::fixy::reborrow(std::move(run), [&ctx, file, start, count](auto lent) noexcept {
                return ::fixy::fs::read_full_at(ctx, file, ::fixy::take_prefix_up_to(std::move(lent), count),
                                                ::fixy::fs::FileOffset{start})
                    .value_or(0);
            });
            run = std::move(chunk_run);
            if (read != count) {
                return ::fixy::none;
            }
            const ::fixy::View<std::byte const> part = run.view().prefix_up_to(count);
            for (std::size_t back = count; back > 0; --back) {
                if (*part.get(back - 1).expect("the position is inside the chunk") == std::byte{'\n'}) {
                    return ::fixy::some(::fixy::fs::FileOffset{start + static_cast<std::int64_t>(back)});
                }
            }
            end = start;
        }
        return ::fixy::some(::fixy::fs::FileOffset{0});
    });
}

// Cuts a torn last record of the file at path under dir, which file holds
// open for appends, and gives the size of its whole lines: the offset after
// its last newline.  A writer ends each record with a newline in the same
// write, so the bytes after the last newline are a record that a crash or a
// failed write tore.  Without the cut, the next record joins them, and the
// line of the two holds no record that a reader can trust.  The cut is
// durable before the next append.  A file of an append open holds no read
// right, so the scan opens the file again for reading.  A read, a cut or a
// sync that fails gives none, and the caller appends nothing.
template <class Ctx, class AppendFile>
[[nodiscard]] ::fixy::Option<::fixy::fs::FileOffset>
cut_torn_record_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir, ::fixy::CText path,
                 AppendFile const& file) noexcept {
    const std::int64_t size = ::fixy::fs::file_size(ctx, file.ref()).value_or(-1);
    if (size < 0) return ::fixy::none;
    if (size == 0) return ::fixy::some(::fixy::fs::FileOffset{0});
    using ReadFile = ::fixy::fs::FileOf<::fixy::fs::read_only>;
    const ::fixy::Option<::fixy::fs::FileOffset> whole =
        ::fixy::fs::mint_file_at<::fixy::fs::read_only>(ctx, dir.ref(), path)
            .match(
                [&](::fixy::Linear<ReadFile> opened) {
                    const ReadFile reader = std::move(opened).consume();
                    return whole_lines_size_(ctx, reader.ref(), ::fixy::fs::FileOffset{size});
                },
                [](::foundation::os::Errno) -> ::fixy::Option<::fixy::fs::FileOffset> { return ::fixy::none; });
    for (const ::fixy::fs::FileOffset good : whole) {
        if (good.raw() == size) return ::fixy::some(good);
        const bool is_cut = ::fixy::fs::set_file_size(ctx, file.ref(), good).is_ok()
                         && ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file.ref()).is_ok();
        if (!is_cut) return ::fixy::none;
        return ::fixy::some(good);
    }
    return ::fixy::none;
}

// Atomic append for the monotonically-growing files: the event log and
// the session-event index.  O_APPEND makes the kernel advance the offset
// to end of file before writing, and POSIX makes that write atomic
// within one filesystem block, 4096 bytes on ext4 and XFS.  The records
// are at most 64 and 96 bytes, so they sit inside the window.  Each record
// is one line with its newline.
//
// The parent-dir fsync only matters for the first record, which creates
// the file.  Later records mutate an existing inode, where fdatasync
// alone would do.
//
// Write-tmp-and-rename is the alternative and it loses here: logs grow
// without bound and a rename does not compose with an append.  A crash or
// a failed write can tear a record, and the torn bytes have no newline.
// The append first cuts them (cut_torn_record_), so the record goes after
// the last whole line, and a reader skips a last line with no newline.  The
// commit of a torn record did not happen, and HEAD did not move, because a
// commit writes HEAD only after its log record is durable.
//
// A write or a flush that fails cuts the file back to its whole lines.  A
// failed write can leave a part of the record, and the cut removes it.  The
// cut can fail too, and the next append then cuts the torn bytes.  One
// writer appends to a file at a time: the log has the one owner of the
// store, and the index of a session has the one writer of the session.  A
// second writer could append between the scan and the cut of the first.
//
// The doors retry EINTR on write, fdatasync, fsync and ftruncate.  Returns
// true only when both the record bytes and the directory entry are durable.
// The fsync reaches the directory of the file only when path is one name in
// dir, so a path with a slash ends the process.
template <class Ctx>
[[nodiscard]] bool append_entry_at_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir, ::fixy::CText path,
                                    ::fixy::TextView record) noexcept {
    CRUCIBLE_PRE(::fixy::find(path.view(), '/').is_none());
    CRUCIBLE_PRE(
        ::fixy::find(record, '\n').is_some_and([&record](std::size_t at) noexcept { return at + 1 == record.size(); }));
    using WriteAppend = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteAppend>;
    // With O_APPEND and a record smaller than a block, a write either fails
    // outright or writes every byte.  The door writes again after a short
    // write on a signal, and each write is a fresh kernel-atomic O_APPEND
    // write.
    using AppendFile = ::fixy::fs::FileOf<WriteAppend>;
    const bool is_flushed =
        ::fixy::fs::mint_file_at<WriteAppend>(ctx, dir.ref(), path)
            .match(
                [&](::fixy::Linear<AppendFile> opened) {
                    const AppendFile file = std::move(opened).consume();
                    for (const ::fixy::fs::FileOffset whole : cut_torn_record_(ctx, dir, path, file)) {
                        const bool is_durable =
                            ::fixy::fs::write_full(ctx, file.ref(), record).is_ok()
                            && ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file.ref()).is_ok();
                        if (!is_durable) {
                            static_cast<void>(
                                ::fixy::fs::set_file_size(ctx, file.ref(), whole).is_ok()
                                && ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file.ref()).is_ok());
                        }
                        return is_durable;
                    }
                    return false;
                },
                [](::foundation::os::Errno) { return false; });
    // Makes the file's existence durable for the first record.
    return is_flushed && ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir.ref()).is_ok();
}

// Opens the directory at path under dir, and gives an empty handle when it
// does not open.
template <class Ctx>
[[nodiscard]] ::fixy::fs::DirectoryHandle open_level_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir,
                                                      ::fixy::CText path) noexcept {
    return ::fixy::fs::open_directory_at(ctx, dir.ref(), path)
        .match([](::fixy::fs::DirectoryHandle opened) { return opened; },
               [](::foundation::os::Errno) { return ::fixy::fs::DirectoryHandle{}; });
}

// Makes the directory name under dir when it is missing, syncs dir, and
// opens the directory.  A new directory is a new entry of dir, and the sync
// of dir makes that entry durable.  The sync runs also when the directory
// is there, because the process that made it can have ended before its own
// sync.  An empty handle stands for a directory that does not open, or for a
// sync that fails.  The store owns each directory that it opens here.
template <class Ctx>
[[nodiscard]] ::fixy::fs::DirectoryHandle
open_durable_directory_at_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir, ::fixy::CText name) noexcept {
    static_cast<void>(::fixy::fs::make_directory_at(ctx, dir.ref(), name).is_ok());
    if (::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir.ref()).is_err()) {
        return ::fixy::fs::DirectoryHandle{};
    }
    return open_level_(ctx, dir, name);
}

// The reason of the error of a make or an open of a part of the root.
[[nodiscard]] Cipher::OpenError open_error_of_(::foundation::os::Errno error) noexcept {
    using ::foundation::os::Errno;
    if (error == Errno::enoent) return Cipher::OpenError::missing;
    if (error == Errno::enotdir || error == Errno::eloop) return Cipher::OpenError::not_a_directory;
    if (error == Errno::eacces || error == Errno::eperm) return Cipher::OpenError::no_permission;
    return Cipher::OpenError::open_failed;
}

// Opens the directory at the full path prefix, which ends in a slash.  The
// slash makes the open follow a link at the last part, so a part of the root
// that is a link opens as the directory of its target.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::fs::DirectoryHandle, Cipher::OpenError>
open_prefix_(Ctx const& ctx, Cipher::RootPath const& prefix) noexcept {
    return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(prefix.c_text().view()))
        .map_err([](::fixy::PathTraversalError) noexcept { return Cipher::OpenError::path_refused; })
        .and_then([&](::fixy::Path<::fixy::tags::source::Sanitized> path) noexcept {
            return ::fixy::fs::open_directory(ctx, path).map_err(open_error_of_);
        });
}

// Makes the directory name under dir, and syncs dir, which holds the new
// entry.  A name that is there gives eexist, and the make then gives no
// error, so the open after it gives the reason of a name that is no
// directory.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, Cipher::OpenError>
make_part_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& dir, ::fixy::CText name) noexcept {
    using Made = ::fixy::Result<::fixy::Unit, Cipher::OpenError>;
    return ::fixy::fs::make_directory_at(ctx, dir.ref(), name)
        .match(
            [&](::fixy::Unit) -> Made {
                return ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir.ref())
                    .map_err([](::foundation::os::Errno) noexcept { return Cipher::OpenError::open_failed; });
            },
            [](::foundation::os::Errno error) -> Made {
                if (error == ::foundation::os::Errno::eexist) return ::fixy::Unit{};
                return ::fixy::err(open_error_of_(error));
            });
}

// True when opened holds the error reason.
[[nodiscard]] bool holds_error_(::fixy::Result<::fixy::fs::DirectoryHandle, Cipher::OpenError> const& opened,
                                Cipher::OpenError reason) noexcept {
    return opened.is_err_and([reason](Cipher::OpenError const& given) noexcept { return given == reason; });
}

// Opens the directory at prefix as open_prefix_ does, but a directory that
// opens only for search gives an empty handle.  The open of the root needs
// search permission on each parent and read permission on the root alone,
// so the walk goes on past such a parent.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::fs::DirectoryHandle, Cipher::OpenError>
open_searchable_(Ctx const& ctx, Cipher::RootPath const& prefix) noexcept {
    ::fixy::Result<::fixy::fs::DirectoryHandle, Cipher::OpenError> opened = open_prefix_(ctx, prefix);
    if (holds_error_(opened, Cipher::OpenError::no_permission)) return ::fixy::fs::DirectoryHandle{};
    return opened;
}

// Opens the part at prefix, and makes it under parent when it is missing.
// A missing part under a parent that opens only for search gives
// no_permission, because the make needs the handle of the parent.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::fs::DirectoryHandle, Cipher::OpenError>
open_part_(Ctx const& ctx, ::fixy::fs::DirectoryHandle const& parent, Cipher::RootPath const& prefix,
           ::fixy::CText name) noexcept {
    using OpenError = Cipher::OpenError;
    ::fixy::Result<::fixy::fs::DirectoryHandle, OpenError> opened = open_searchable_(ctx, prefix);
    if (!holds_error_(opened, OpenError::missing)) return opened;
    if (!parent.is_open()) return ::fixy::err(OpenError::no_permission);
    CRUCIBLE_TRY(_, make_part_(ctx, parent, name));
    return open_searchable_(ctx, prefix);
}

// Makes each directory of root that is missing, from the first part on, as
// mkdir -p does.  The path of the root is configuration that an operator
// gives, and the operator trusts the links of that path.  So the walk opens
// each part by its full path with a slash at its end, and the open follows
// a link at the part, such as /var/run.  The walk starts at "/" for an
// absolute root, and at the current directory for a relative root, so an
// absolute root does not read the current directory.  open_part_ makes a
// missing part under the handle of the part before it, and it gives an empty
// handle for a part that opens only for search.  The open of the root after
// the walk gives the reason of a root that does not open.  The sanitizer of
// the root refused each ".." part.  Each name below the root still opens
// beneath the handle of the root.  The prefix adds "./" and a slash to the
// root, so a root within three bytes of MAX_ROOT_PATH_BYTES gives
// path_refused.  Complexity: one or two opens of a full path for each part,
// one make and one sync for each part that the walk makes, and one copy of
// each character.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, Cipher::OpenError> make_root_directories_(Ctx const& ctx,
                                                                                     ::fixy::TextView root) noexcept {
    using OpenError = Cipher::OpenError;
    Cipher::RootPath prefix{};
    const ::fixy::TextView start = ::fixy::starts_with(root, "/") ? ::fixy::TextView{"/"} : ::fixy::TextView{"./"};
    CRUCIBLE_TRY(
        _, prefix.append(start).map_err([](::foundation::os::Errno) noexcept { return OpenError::path_refused; }));
    CRUCIBLE_TRY(start_dir, open_searchable_(ctx, prefix));
    ::fixy::fs::DirectoryHandle level = std::move(start_dir);
    ::fixy::TextView rest = root;
    while (rest.size() != 0) {
        const std::size_t length = ::fixy::find(rest, '/').value_or(rest.size());
        const ::fixy::TextView part = rest.window(0, length).expect("a find gives an offset in the text");
        const std::size_t after = ::fixy::min(length + 1, rest.size());
        rest = rest.window(after, rest.size() - after).expect("the offset after the part is in the text");
        if (part.size() == 0 || part == ::fixy::TextView{"."}) continue;
        Cipher::RootPath name{};
        const bool is_named = name.append(part).is_ok() && prefix.append(part).is_ok() && prefix.append('/').is_ok();
        if (!is_named) return ::fixy::err(OpenError::path_refused);
        const ::fixy::Option<OpenError> refusal =
            open_part_(ctx, level, prefix, name.c_text())
                .match(
                    [&level](::fixy::fs::DirectoryHandle next_level) -> ::fixy::Option<OpenError> {
                        level = std::move(next_level);
                        return ::fixy::none;
                    },
                    [](OpenError reason) -> ::fixy::Option<OpenError> { return ::fixy::some(reason); });
        for (const OpenError reason : refusal)
            return ::fixy::err(reason);
    }
    return ::fixy::Unit{};
}

// The bytes of path under dir, when the file holds more than zero and at
// most maximum bytes.  A file that does not open gives file_open_failed, and a
// file of another size or a short read gives file_size.  Complexity: one
// read of the file.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::HeapArray<std::byte>, LoadError>
read_batch_at_(Ctx const& ctx, ::foundation::effects::Alloc allocation, ::fixy::fs::DirectoryHandle const& dir,
               ::fixy::CText path, std::size_t maximum) noexcept {
    CRUCIBLE_TRY(opened, ::fixy::fs::mint_file_at<::fixy::fs::read_only>(ctx, dir.ref(), path)
                             .map_err([](::foundation::os::Errno) noexcept { return LoadError::file_open_failed; }));
    const ::fixy::fs::FileOf<::fixy::fs::read_only> file = std::move(opened).consume();
    const std::int64_t size = ::fixy::fs::file_size(ctx, file.ref()).value_or(0);
    if (size <= 0 || static_cast<std::uint64_t>(size) > maximum) {
        return ::fixy::err(LoadError::file_size);
    }
    ::fixy::HeapArray<std::byte> bytes =
        ::fixy::mint_heap_array(allocation, static_cast<std::size_t>(size), std::byte{0});
    auto [count_read, filled] = ::fixy::with_mut(std::move(bytes), [&ctx, &file](auto run) noexcept {
        return ::fixy::fs::read_full(ctx, file.ref(), std::move(run)).value_or(0);
    });
    if (count_read != filled.size()) {
        return ::fixy::err(LoadError::file_size);
    }
    return std::move(filled);
}

// True when the object leaf in shard_dir is a sound stored image of the region
// of hash (stored_region_fault of crucible/Serialize.h).  An object that a
// crash or a second writer cut, or that holds the bytes of another hash, is
// not sound, so the store does not count it as durable.  Complexity: one read
// of the object and linear in its size.
template <class Ctx>
[[nodiscard]] bool is_sound_object_(Ctx const& ctx, ::foundation::effects::Alloc allocation,
                                    ::fixy::fs::DirectoryHandle const& shard_dir, ::fixy::CText leaf,
                                    ContentHash hash) noexcept {
    const ::fixy::Result<::fixy::HeapArray<std::byte>, LoadError> read =
        read_batch_at_(ctx, allocation, shard_dir, leaf, Cipher::MAX_OBJECT_BYTES);
    return read.is_ok_and([hash](::fixy::HeapArray<std::byte> const& bytes) noexcept {
        return stored_region_fault(bytes.view(), hash).is_none();
    });
}

// An owned copy of bytes in new storage of allocation.  Complexity: one
// allocation and a copy of each byte.
[[nodiscard]] ::fixy::HeapArray<std::byte> copy_bytes_(::foundation::effects::Alloc allocation,
                                                       ::fixy::View<const std::byte> bytes) noexcept {
    return ::fixy::mint_heap_array_copy<std::byte>(allocation, bytes);
}

// The first line of a text, without its newline, and the text after the
// newline.  A text with no newline is one line, and no newline ends it.
struct LineSplit {
    ::fixy::TextView line{};
    ::fixy::TextView rest{};
    // True when a newline ends the line.  A writer of the log and of an index
    // ends each record with a newline in the same write, so a last line with
    // no newline is a record that a crash or a failed write tore.  The next
    // append cuts it (cut_torn_record_), and a reader skips it.
    bool is_terminated = false;
};

[[nodiscard]] LineSplit split_line_(::fixy::TextView text) noexcept {
    ::fixy::Option<std::size_t> newline = ::fixy::find(text, '\n');
    const bool is_terminated = newline.is_some();
    const std::size_t length = std::move(newline).value_or(text.size());
    const std::size_t next = ::fixy::min(length + 1, text.size());
    return LineSplit{
        .line = text.window(0, length).value_or(::fixy::TextView{}),
        .rest = text.window(next, text.size() - next).value_or(::fixy::TextView{}),
        .is_terminated = is_terminated,
    };
}

// The text before the first comma of a text, and the text after it, or
// none when the text holds no comma.
struct CommaSplit {
    ::fixy::TextView head{};
    ::fixy::TextView tail{};
};

[[nodiscard]] ::fixy::Option<CommaSplit> split_at_comma_(::fixy::TextView text) noexcept {
    const std::size_t comma = ::fixy::find(text, ',').value_or(text.size());
    if (comma == text.size()) return ::fixy::none;
    return ::fixy::some(CommaSplit{
        .head = text.window(0, comma).value_or(::fixy::TextView{}),
        .tail = text.window(comma + 1, text.size() - comma - 1).value_or(::fixy::TextView{}),
    });
}

// The value of a field of decimal digits.  The parse of the base reads a
// "0x" prefix as hexadecimal digits, and a field of the store holds no
// prefix, so a field with the prefix gives none.
[[nodiscard]] ::fixy::Option<std::uint64_t> parse_decimal_(::fixy::TextView field) noexcept {
    if (::fixy::starts_with(field, "0x")) return ::fixy::none;
    return ::fixy::parse<std::uint64_t>(field).ok();
}

// The value of a field of bare hexadecimal digits.
[[nodiscard]] ::fixy::Option<std::uint64_t> parse_hex_field_(::fixy::TextView field) noexcept {
    return ::foundation::core::parse_hex<std::uint64_t>(field).ok();
}

// One line of the head log: "<step>,<16 hexadecimal digits>,<order>".
struct LogLine {
    std::uint64_t step = 0;
    std::uint64_t hash = 0;
    std::uint64_t order = 0;
};

[[nodiscard]] ::fixy::Option<LogLine> parse_log_line_(::fixy::TextView line) noexcept {
    CRUCIBLE_TRY(first_split, split_at_comma_(line));
    CRUCIBLE_TRY(hash_split, split_at_comma_(first_split.tail));
    CRUCIBLE_TRY(step, parse_decimal_(first_split.head));
    CRUCIBLE_TRY(hash, parse_hex_field_(hash_split.head));
    CRUCIBLE_TRY(order, parse_decimal_(hash_split.tail));
    return ::fixy::some(LogLine{.step = step, .hash = hash, .order = order});
}

// One line of a session-event index:
// "<first_step>,<last_step>,<count>,<16 hexadecimal digits>".
struct IndexLine {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    std::uint64_t count = 0;
    std::uint64_t hash = 0;
};

[[nodiscard]] ::fixy::Option<IndexLine> parse_index_line_(::fixy::TextView line) noexcept {
    CRUCIBLE_TRY(first_split, split_at_comma_(line));
    CRUCIBLE_TRY(last_split, split_at_comma_(first_split.tail));
    CRUCIBLE_TRY(count_split, split_at_comma_(last_split.tail));
    CRUCIBLE_TRY(first, parse_decimal_(first_split.head));
    CRUCIBLE_TRY(last, parse_decimal_(last_split.head));
    CRUCIBLE_TRY(count, parse_decimal_(count_split.head));
    CRUCIBLE_TRY(hash, parse_hex_field_(count_split.tail));
    if (first > last) return ::fixy::none;
    return ::fixy::some(IndexLine{.first = first, .last = last, .count = count, .hash = hash});
}

// True when lines holds a line equal to line.  The lines are in the order of
// their first steps, so the search halves its range at each step.
// Complexity: O(log n) in the number of lines.
[[nodiscard]] bool holds_index_line_(::fixy::View<IndexLine const> lines, IndexLine const& line) noexcept {
    std::size_t low = 0;
    std::size_t high = lines.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (lines.get(middle).expect("the middle is inside the lines")->first < line.first) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    for (auto const found : lines.get(low)) {
        return found->first == line.first && found->last == line.last && found->count == line.count
            && found->hash == line.hash;
    }
    return false;
}

// The name of the batch file of hash in its session directory:
// "<16 hexadecimal digits>.cfed".
[[nodiscard]] EntryPath batch_entry_(std::uint64_t hash) noexcept {
    const ::fixy::FixedText<16> digits = hex_digits_(hash);
    EntryPath path{};
    path.append(digits.view()).expect("the batch name fits its path");
    path.append(::fixy::TextView{".cfed"}).expect("the batch name fits its path");
    return path;
}

// Conservative upper bound on the serialized size of a RegionNode.
//
// The four unconditional terms sum to 384 bytes, so the result is
// positive by construction.  Stating that in the return type means a
// refactor that drops the headroom or makes a header conditional trips
// the mint's precondition here, instead of silently handing the caller
// a zero-length buffer to serialize into.
::fixy::Positive<std::size_t> estimate_serial_size(RegionNode const& region) {
    size_t sz = 64;  // header
    sz += 32;  // region fixed fields
    sz += detail_ser::kRegionChecksumBytes;
    for (const DagRef<MemoryPlan const>& plan : region.plan) {
        sz += 64 + plan->num_slots() * sizeof(TensorSlot);
    }
    for (const TraceEntry& te : region.ops.view()) {
        const size_t n_in = te.num_inputs();
        const size_t n_out = te.num_outputs();
        const size_t n_sca = te.num_scalar_args();
        sz += 40;  // fixed per-op header
        sz += (n_in + n_out) * sizeof(TensorMeta);
        sz += n_sca * sizeof(int64_t);
        sz += n_in * sizeof(uint32_t);  // input_trace_indices
        sz += n_in * sizeof(uint32_t);  // input_slot_ids
        sz += n_out * sizeof(uint32_t);  // output_slot_ids
    }
    return ::fixy::mint_refined<::fixy::positive>(sz + 256);  // headroom
}

}  // namespace

// Each receiver of an open view below takes OpenView, the view on the erased
// identity that mint_open_view gives.  The bodies are in this file and are not
// templates, so no receiver can take the brand of a caller.
ContentHash Cipher::store_(FileCtx const& file_ctx, OpenView const&, ContentAddressedRegionPayload payload) {
    CRUCIBLE_LET_ELSE(held, payload.get(), ContentHash{});
    const RegionNode& region = *held;
    const ContentHash hash = region.content_hash;
    if (!hash) return ContentHash{};

    // The object is objects/<shard>/<leaf>.  The shard directory holds the
    // objects of one first byte of the hash, and it holds the rename of the
    // object, so its sync makes the object durable.  The open makes the
    // shard when it is missing and syncs objects, which holds the shard.
    const EntryPath shard = object_shard_(hash.raw());
    const EntryPath leaf = object_leaf_(hash.raw());
    const ::fixy::fs::DirectoryHandle shard_dir = with_file_ctx_(file_ctx, [&](auto ctx) {
        return open_durable_directory_at_(ctx, open_level_(ctx, root_directory_, "objects"), shard.c_text());
    });
    if (!shard_dir.is_open()) return ContentHash{};

    // An object of the hash that is there counts as durable only when it is
    // a sound image of the hash: a crash or a second writer can leave a cut
    // object.  The skip path also flushes the bytes and the entry of the
    // object.  Another process may have written them and died before the
    // kernel flushed them, and this process is about to return the hash as
    // durable.
    const auto is_sound_and_flushed = [&](auto ctx) {
        return is_sound_object_(ctx, allocation_, shard_dir, leaf.c_text(), hash)
            && flush_entry_at_(ctx, shard_dir, leaf.c_text())
            && ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, shard_dir.ref()).is_ok();
    };
    const bool is_present = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::has_entry_at(ctx, shard_dir.ref(), leaf.c_text()).value_or(false);
    });
    if (is_present && with_file_ctx_(file_ctx, is_sound_and_flushed)) return hash;

    const size_t cap = estimate_serial_size(region).value();
    auto [n, image] =
        ::fixy::with_mut(::fixy::mint_heap_array(allocation_, cap, std::byte{0}),
                         [&region](auto run) noexcept { return serialize_region(::fixy::to(region), std::move(run)); });
    if (n == 0) return ContentHash{};
    const ::fixy::View<const std::byte> written =
        image.view().window(0, n).expect("the writer writes inside its image");

    // The write puts the image in a temporary file of this writer in the
    // shard, flushes its bytes, renames it onto the object, and syncs the
    // shard.  A reader then sees the whole object or no object, and a hash
    // that this function returns is loadable after a crash.  A new object
    // keeps an object that another writer made in the meantime: a sound one
    // counts as durable, and a damaged one takes the image of this writer.  A
    // damaged object that was there before takes it at once.
    const auto write_object = [&](EntryCommit commit) {
        return with_file_ctx_(file_ctx, [&](auto const& ctx) {
            return write_entry_at_(ctx, shard_dir, leaf.c_text().view(), written, commit);
        });
    };
    EntryWrite stored = write_object(is_present ? EntryCommit::replace : EntryCommit::keep_existing);
    if (stored == EntryWrite::exists) {
        if (with_file_ctx_(file_ctx, is_sound_and_flushed)) return hash;
        stored = write_object(EntryCommit::replace);
    }
    if (stored != EntryWrite::written) {
        return ContentHash{};
    }

    remember_cached_bytes(hash, written);
    return hash;
}

::fixy::cipher_tier::Hot<ContentHash> Cipher::publish_hot(OpenView const&,
                                                          ContentAddressedRegionPayload /*payload*/) noexcept {
    return cipher::mint_promote<::fixy::CipherTierTag_v::Cold, ::fixy::CipherTierTag_v::Hot>(
        ::fixy::mint_band<::fixy::cipher_tier::Cold<ContentHash>>(ContentHash{}));
}

::fixy::cipher_tier::Cold<ContentHash> Cipher::publish_cold(OpenView const&,
                                                            ContentAddressedRegionPayload /*payload*/) noexcept {
    return cipher::mint_demote<::fixy::CipherTierTag_v::Hot, ::fixy::CipherTierTag_v::Cold>(
        ::fixy::mint_band<::fixy::cipher_tier::Hot<ContentHash>>(ContentHash{}));
}

Cipher::LoadResult Cipher::load_content_addressed_(FileCtx const& file_ctx, OpenView const&,
                                                   ::foundation::effects::Alloc a, ContentHash content_hash,
                                                   ::fixy::BumpArena<DagArena>& arena) const {
    if (!content_hash) return ::fixy::err(LoadError::none_hash);

    // A cached image that gives no region of the hash falls through to the
    // file, and the file gives the reason of a refusal.
    const ::fixy::View<const std::byte> cached = cached_bytes(content_hash);
    if (cached.size() != 0) {
        for (const LoadedRegionNode& loaded_region : deserialize_region(a, cached, arena).ok()) {
            if (loaded_region.value()->content_hash == content_hash) {
                return LoadedContentAddressedRegionPayload{loaded_region.value(), true};
            }
        }
    }

    // The read refuses a missing object, an empty object and an object
    // past MAX_OBJECT_BYTES, and the mint states the same ceiling again.
    // If the read is later changed and drops its bound, the mint's
    // precondition still fires.
    const EntryPath object = object_entry_(content_hash.raw());
    ::fixy::Result<::fixy::HeapArray<std::byte>, LoadError> read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return read_batch_at_(ctx, a, root_directory_, object.c_text(), MAX_OBJECT_BYTES);
    });
    CRUCIBLE_TRY(image, std::move(read));
    [[maybe_unused]] const ValidatedObjectSize validated_len =
        ::fixy::mint_refined<::fixy::bounded_above<MAX_OBJECT_BYTES>>(image.size());

    CRUCIBLE_TRY(loaded_region, deserialize_region(a, image.view(), arena));
    // The file name is the key, and anyone who writes the store can put
    // any region under any name.  A region whose own hash is not the key
    // is refused, the same as on the cache path above, so a lookup never
    // returns another region.
    if (loaded_region.value()->content_hash != content_hash) return ::fixy::err(LoadError::object_hash_mismatch);
    remember_cached_bytes(content_hash, image.view());
    return LoadedContentAddressedRegionPayload{loaded_region.value(), false};
}

Cipher::PersistResult Cipher::persist_session_events_(FileCtx const& file_ctx, OpenView const&,
                                                      ::foundation::effects::Alloc allocation,
                                                      ::fixy::View<SessionEvent const> events) {
    if (events.size() == 0) return ::fixy::err(PersistRefusal::empty_batch);

    if (events.size() > MAX_SESSION_EVENT_BATCH_EVENTS) {
        return ::fixy::err(PersistRefusal::batch_above_limit);
    }

    const ::fixy::session::SessionTagId session = events.get(0).expect("the batch holds an event")->session();
    const uint64_t first_step = events.get(0).expect("the batch holds an event")->step_id().raw();
    const uint64_t last_step = events.get(events.size() - 1).expect("the batch holds an event")->step_id().raw();
    bool has_previous = false;
    uint64_t previous_step = 0;
    for (const SessionEvent& event : events) {
        if (event.session() != session) [[unlikely]] {
            return ::fixy::err(PersistRefusal::mixed_sessions);
        }
        const uint64_t step = event.step_id().raw();
        if (has_previous && (previous_step == ::fixy::max_of<uint64_t> || previous_step + 1 != step)) [[unlikely]] {
            return ::fixy::err(PersistRefusal::steps_not_consecutive);
        }
        previous_step = step;
        has_previous = true;
    }

    const std::size_t payload_bytes = events.size() * sizeof(SessionEvent);
    if (payload_bytes > MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
        return ::fixy::err(PersistRefusal::batch_above_limit);
    }

    // Each event goes to the batch through its own encoder, which is
    // the one route from an event to bytes.  The hash consumes those
    // bytes structurally.  Complexity: linear in the number of events.
    ::fixy::HeapArray<std::byte> payload =
        ::fixy::with_mut(::fixy::mint_heap_array(allocation, payload_bytes, std::byte{0}), [events](auto run) noexcept {
            std::size_t position = 0;
            for (const SessionEvent& event : events) {
                const auto record = event.encode();
                run = ::fixy::reborrow(std::move(run), [&record, position](auto lent) noexcept {
                    ::fixy::copy(
                        ::fixy::take_window<sizeof(SessionEvent)>(std::move(lent), position * sizeof(SessionEvent))
                            .expect("the payload holds a record for each event"),
                        ::fixy::bytes(record));
                });
                ++position;
            }
        });
    const ContentHash hash = session_event_batch_hash(payload.view());
    const cipher::federation::FederationKey key = session_event_batch_key(payload.view());

    // The entry fills its buffer: the header and then the payload.
    auto [entry_written, encoded_entry] = ::fixy::with_mut(
        ::fixy::mint_heap_array(allocation, cipher::federation::FEDERATION_HEADER_BYTES + payload_bytes, std::byte{0}),
        [&key, &payload](auto run) noexcept {
            return cipher::federation::serialize_federation_entry(std::move(run), key, payload.view())
                .map_err(
                    [](cipher::federation::FederationError) noexcept { return PersistRefusal::entry_encode_failed; });
        });
    CRUCIBLE_TRY(_, std::move(entry_written));
    ::fixy::HeapArray<std::byte> const encoded = std::move(encoded_entry);

    // The index line: "<first_step>,<last_step>,<count>,<16-hex hash>\n".
    // The longest form is 20 + 1 + 20 + 1 + 20 + 1 + 16 + 1 = 80 bytes,
    // well under the 4096-byte window in which POSIX makes a single
    // O_APPEND write to a regular file atomic.
    //
    // A crash in the middle of a record leaves a torn last line with no
    // newline.  The next append cuts it, and load_session_events skips it,
    // so a crash drops one index entry.  The batch file itself is already
    // on disk, and the next persist_session_events call re-appends the
    // entry.
    const ::fixy::FixedText<16> hash_digits = hex_digits_(hash.raw());
    ::fixy::FixedText<96> record{};
    const ::fixy::TextView index_line =
        ::fixy::format(record, "{},{},{},{}\n", first_step, last_step, events.size(), hash_digits.view())
            .expect("an index line fits 96 characters");

    const EntryPath batch = batch_entry_(hash.raw());
    const EntryPath session_name = session_name_(session);

    // The session subdirectory anchors the batch and the index at two
    // levels: root dirfd, then session dirfd, then the leaf under
    // O_NOFOLLOW.  Substituting the intermediate session_events directory
    // needs write access to the Cipher root, which is outside the threat
    // model.  The open of each level makes it when it is missing and syncs
    // the directory that holds it.  A batch file of the hash must hold the
    // same bytes, and the write makes a new batch file durable before the
    // index names it.  The write keeps a batch file that another writer made
    // in the meantime, and that file must hold the same bytes too.  A batch
    // file of the hash that does not read gives the LoadError of its read.
    using Indexed = ::fixy::Result<::fixy::Unit, PersistError>;
    Indexed indexed = with_file_ctx_(file_ctx, [&](auto ctx) -> Indexed {
        const ::fixy::fs::DirectoryHandle session_dir = open_durable_directory_at_(
            ctx, open_durable_directory_at_(ctx, root_directory_, "session_events"), session_name.c_text());
        if (!session_dir.is_open()) return ::fixy::err(PersistRefusal::session_directory_failed);
        const auto check_stored_batch = [&]() -> Indexed {
            CRUCIBLE_TRY(stored, read_batch_at_(ctx, allocation, session_dir, batch.c_text(), encoded.size()));
            if (!::fixy::equal(stored.view(), encoded.view())) return ::fixy::err(PersistRefusal::stored_batch_differs);
            return ::fixy::Unit{};
        };
        const bool is_batch_present = ::fixy::fs::has_entry_at(ctx, session_dir.ref(), batch.c_text()).value_or(false);
        if (is_batch_present) {
            CRUCIBLE_TRY(_, check_stored_batch());
        } else {
            const EntryWrite written =
                write_entry_at_(ctx, session_dir, batch.c_text().view(), ::fixy::View<std::byte const>{encoded.view()},
                                EntryCommit::keep_existing);
            if (written == EntryWrite::exists) {
                CRUCIBLE_TRY(_, check_stored_batch());
            } else if (written != EntryWrite::written) {
                return ::fixy::err(PersistRefusal::batch_write_failed);
            }
        }
        if (!append_entry_at_(ctx, session_dir, "index", index_line)) {
            return ::fixy::err(PersistRefusal::index_append_failed);
        }
        return ::fixy::Unit{};
    });
    CRUCIBLE_TRY(_, std::move(indexed));
    return hash;
}

Cipher::SessionEventLoad Cipher::load_session_events_(FileCtx const& file_ctx, OpenView const&,
                                                      ::foundation::effects::Alloc allocation,
                                                      ::fixy::session::SessionTagId session,
                                                      ::fixy::session::StepId from_step) const {
    // The index bounds no count of events, so the list of the load takes
    // the largest count of a View.
    SessionEventLoad loaded{};
    const EntryPath session_directory = session_entry_(session);
    CRUCIBLE_LET_ELSE(session_dir,
                      with_file_ctx_(file_ctx,
                                     [&](auto const& ctx) {
                                         return ::fixy::fs::open_directory_at(ctx, root_directory_.ref(),
                                                                              session_directory.c_text());
                                     }),
                      loaded);

    // The index has no bound of its own, so the text takes the largest
    // count of a view.  A failed read leaves the bytes that it read in the
    // text, and the parse reads them: a torn last line has no newline, and
    // the loop stops at it.
    ::fixy::GrowText index_text = ::fixy::mint_grow_text(::fixy::max_of<std::size_t>);
    static_cast<void>(with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, session_dir.ref(), "index", index_text).is_ok();
    }));
    const ::fixy::HeapText index = ::fixy::freeze(std::move(index_text));

    uint64_t highest_loaded = from_step.raw();
    bool have_loaded = false;
    // The lines of the loaded batches.  Each loaded batch starts after the
    // last step that the load held before it, so the lines are in the order
    // of their first steps.
    ::foundation::core::GrowList<IndexLine> loaded_lines =
        ::foundation::core::mint_grow_list<IndexLine>(::fixy::max_of<std::size_t>);

    // The events of the batch of one index line, or the reason that the load
    // skips the batch.  The hashes prove only that the batch is the one its
    // writer stored, and anyone who writes the store can compute them.  So
    // each record goes through the event decoder, which is the one route
    // from bytes to an event and refuses a record with a byte outside its
    // field.  Complexity: one read of the batch file and linear in its size.
    using CheckedBatch = ::fixy::Result<::fixy::HeapArray<SessionEvent>, LoadError>;
    const auto read_checked_batch = [&](IndexLine const& index_line) noexcept -> CheckedBatch {
        const uint64_t first = index_line.first;
        const uint64_t last = index_line.last;
        const uint64_t count = index_line.count;
        if (count == 0 || count > MAX_SESSION_EVENT_BATCH_EVENTS || (last - first + 1u) != count) {
            return ::fixy::err(LoadError::batch_count_mismatch);
        }

        const ContentHash hash{index_line.hash};
        const EntryPath batch = batch_entry_(index_line.hash);
        CRUCIBLE_TRY(bytes, with_file_ctx_(file_ctx, [&](auto const& ctx) noexcept {
                         return read_batch_at_(ctx, allocation, session_dir, batch.c_text(),
                                               cipher::federation::FEDERATION_HEADER_BYTES
                                                   + MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES);
                     }));

        CRUCIBLE_TRY(view, cipher::federation::deserialize_untrusted_federation_entry(
                               bytes.view(), cipher::federation::FEDERATION_UNIVERSE_CARDINALITY)
                               .map_err([](cipher::federation::FederationError) noexcept {
                                   return LoadError::batch_entry_malformed;
                               }));
        const bool has_named_hashes =
            session_event_batch_hash(view.payload) == hash && view.header.key == session_event_batch_key(view.payload);
        if (!has_named_hashes) return ::fixy::err(LoadError::batch_hash_mismatch);
        if ((view.payload.size() % sizeof(SessionEvent)) != 0u || view.payload.size() / sizeof(SessionEvent) != count) {
            return ::fixy::err(LoadError::batch_count_mismatch);
        }

        CRUCIBLE_TRY(events, ::fixy::session::decode_session_log(allocation, view.payload)
                                 .map_err([](::fixy::session::EventLogDecodeFailure) noexcept {
                                     return LoadError::field_out_of_range;
                                 }));
        // The count check above gives the batch count events, and count is
        // above zero.
        const bool has_named_range =
            events.view().get(0).expect("the batch holds an event")->step_id().raw() == first
            && events.view().get(events.size() - 1).expect("the batch holds an event")->step_id().raw() == last;
        if (!has_named_range) return ::fixy::err(LoadError::batch_count_mismatch);
        bool has_previous = false;
        uint64_t previous_step = 0;
        for (const SessionEvent& event : events.view()) {
            const uint64_t step = event.step_id().raw();
            const bool is_out_of_sequence =
                has_previous && (previous_step == ::fixy::max_of<uint64_t> || previous_step + 1u != step);
            if (event.session() != session || is_out_of_sequence) return ::fixy::err(LoadError::batch_steps_mismatch);
            previous_step = step;
            has_previous = true;
        }
        // The steps of the batch are consecutive, so a batch whose first
        // step is at or before the last step of the load overlaps the steps
        // of the load.  The check comes after the read, so a second line of
        // a batch that does not read gives the reason of the read.
        if (have_loaded && first <= highest_loaded) return ::fixy::err(LoadError::batch_steps_mismatch);
        return events;
    };

    // Loads the events of the batch of one index line, or reports the reason
    // that the load skips the batch.
    const auto load_line = [&](IndexLine const& index_line) noexcept {
        if (index_line.last < from_step.raw()) return;
        // A writer that stores a batch again appends its line again, and the
        // load holds the events of that batch.  Each other line whose first
        // step the load holds names a batch that overlaps the events of the
        // load, and the check of the batch skips it with a report.
        const bool is_loaded_again =
            have_loaded && index_line.last <= highest_loaded
            && loaded_lines.with_view([&index_line](::fixy::View<IndexLine const> lines) noexcept {
                   return holds_index_line_(lines, index_line);
               });
        if (is_loaded_again) return;

        read_checked_batch(index_line)
            .match(
                [&](::fixy::HeapArray<SessionEvent> events) noexcept {
                    // The steps of the batch are first, first + 1, ..., last,
                    // and last is at or after from_step.  So the events at
                    // from_step or after are one suffix of the batch, and it
                    // is not empty.
                    const std::uint64_t skipped =
                        from_step.raw() > index_line.first ? from_step.raw() - index_line.first : std::uint64_t{0};
                    const ::fixy::View<SessionEvent const> suffix =
                        events.view().window(skipped, events.size() - skipped).expect("the suffix is inside the batch");
                    for (const SessionEvent& event : suffix) {
                        loaded.events.append(allocation, event).expect("the list of the events has no maximum");
                    }
                    loaded_lines.append(allocation, index_line).expect("the list of the loaded lines has no maximum");
                    highest_loaded = index_line.last;
                    have_loaded = true;
                },
                [&](LoadError reason) noexcept {
                    const ::fixy::FixedText<16> hash_digits = hex_digits_(index_line.hash);
                    ::fixy::report(
                        ::fixy::Sink::Err,
                        "[crucible] the Cipher store skips the session-event batch {} of the steps {} to {}: {}\n",
                        hash_digits.view(), index_line.first, index_line.last, load_error_text(reason));
                    ++loaded.skipped_batches;
                });
    };

    for (::fixy::TextView rest = index.view(); rest.size() != 0;) {
        const LineSplit split = split_line_(rest);
        rest = split.rest;
        // A last line with no newline is a torn line, also when it parses.
        // Its append did not end, and the next append cuts it.
        if (!split.is_terminated) break;
        // A line that does not parse names no batch.  The next persist of its
        // batch appends the line again.
        for (IndexLine const& index_line : parse_index_line_(split.line))
            load_line(index_line);
    }
    return loaded;
}

ContentHash Cipher::hash_at_step(OpenView const&, uint64_t step_id) const {
    if (log_.empty()) return ContentHash{};

    // Find the first entry after step_id, then walk back to the latest
    // event that actually commits the head.  The log entry type is the
    // shared session-event record, which can also carry events that do
    // not move the head.
    std::size_t lo = 0;
    std::size_t hi = log_.size();

    while (lo < hi) {
        auto const mid = ::fixy::midpoint(lo, hi);
        if (log_[mid].step_id().raw() <= step_id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    while (lo > 0) {
        --lo;
        const LogEntry entry = log_[lo];
        if (commits_head_(entry)) {
            return committed_hash_(entry);
        }
    }
    return ContentHash{};
}

ContentHash Cipher::session_event_batch_hash(::fixy::View<std::byte const> bytes) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL ^ 0x53455353494f4e45ULL ^ bytes.size();
    for (const std::byte byte : bytes) {
        h ^= static_cast<uint64_t>(byte);
        h *= 0x100000001b3ULL;
    }
    h = ::foundation::reflect::fmix64(h);
    if (h == 0u) h = 0x53455353494f4e45ULL;
    if (h == ::fixy::max_of<uint64_t>) --h;
    return ContentHash{h};
}

namespace {

// The text whose 32 bytes are the key of the session-event batches.
constexpr ::fixy::TextView kSessionEventBatchKeyText{"crucible cipher session events 2"};

constexpr ::foundation::core::KeyedHashKeyBytes kSessionEventBatchHashKey =
    detail_ser::key_of_text(kSessionEventBatchKeyText);

}  // namespace

cipher::federation::FederationKey Cipher::session_event_batch_key(::fixy::View<std::byte const> bytes) noexcept {
    return cipher::federation::FederationKey{::foundation::core::keyed_hash(kSessionEventBatchHashKey, bytes)};
}

ContentHash Cipher::latest_committed_head() const noexcept {
    std::size_t i = log_.size();
    while (i > 0) {
        --i;
        const LogEntry entry = log_[i];
        if (commits_head_(entry)) {
            return committed_hash_(entry);
        }
    }
    return ContentHash{};
}

Cipher::CommitResult Cipher::advance_head(FileCtx const& file_ctx, OpenView const&, ContentHash content_hash,
                                          uint64_t step_id) {
    // Weakly increasing, not strictly: duplicate step ids are accepted
    // at this gate.  The strict consecutive-by-one check belongs to the
    // session-event batch invariant, not to the head log.  The log of an
    // earlier run can hold a later step than the caller, so a step out of
    // order is an error of the commit.  A refused step writes nothing and
    // takes no place, and load_log refuses a step above the ceiling, so each
    // commit that this function makes loads again.
    if (step_id > MAX_COMMIT_STEP) {
        return ::fixy::err(CommitError::step_above_limit);
    }
    for (const ::fixy::session::StepId last : last_committed_step()) {
        if (step_id < last.raw()) return ::fixy::err(CommitError::step_order);
    }
    // The commit takes the next place before its append.  A write that
    // passed before a failed flush can leave the record in the file, and a
    // reopen then reads that commit.  So a failed append keeps its place,
    // and the next commit gets a larger one.
    if (commits_.get() == ::fixy::max_of<std::uint64_t>) {
        return ::fixy::err(CommitError::order_exhausted);
    }
    commits_.bump();
    const ::fixy::session::CipherEventOrder order{commits_.get()};

    // The log record is the commit, and it is durable before HEAD moves.
    // A failed append moves no head, so this store and a reopen agree.
    //
    // Record layout: "<step_id>,<16-hex content_hash>,<order>\n".  The
    // longest form is 20 decimal digits + 1 + 16 + 1 + 20 decimal digits
    // + 1 = 59 bytes, so 64 bytes suffices.  That is well under the
    // 4096-byte window in which POSIX makes a single O_APPEND write to a
    // regular file atomic.
    const ::fixy::FixedText<16> hash_digits = hex_digits_(content_hash.raw());
    ::fixy::FixedText<64> record{};
    const ::fixy::TextView log_line = ::fixy::format(record, "{},{},{}\n", step_id, hash_digits.view(), order.raw())
                                          .expect("a log record fits 64 characters");
    const bool is_appended =
        with_file_ctx_(file_ctx, [&](auto ctx) { return append_entry_at_(ctx, root_directory_, "log", log_line); });
    if (!is_appended) {
        return ::fixy::err(CommitError::log_append);
    }
    log_.append(allocation_,
                SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted, ::fixy::session::StepId{step_id},
                                           ::fixy::session::StateHash{content_hash.raw()}, order));
    head_ = content_hash;

    // HEAD is a copy of the last commit of the log.  A failed write leaves
    // HEAD at an earlier commit, and the open takes the head from the log.
    ::fixy::FixedText<17> head_line{};
    head_line.append(hash_digits.view()).expect("sixteen digits fit the HEAD line");
    head_line.append('\n').expect("the newline fits the HEAD line");
    const bool is_head_written = with_file_ctx_(file_ctx, [&](auto ctx) {
        return write_entry_at_(ctx, root_directory_, "HEAD", head_line.view(), EntryCommit::replace)
            == EntryWrite::written;
    });

    CRUCIBLE_POST(0, head_ == content_hash);
    CRUCIBLE_POST(0, !log_.empty());
    CRUCIBLE_POST(0, log_.back().step_id().raw() == step_id);
    if (!is_head_written) {
        return ::fixy::err(CommitError::head_write);
    }
    return ::fixy::Unit{};
}

// HEAD holds the hash in hexadecimal, with a newline after it.  The digits
// are the characters before the first newline, or the whole text when it
// holds no newline.  A corrupt HEAD gives no hash.
::fixy::Option<ContentHash> Cipher::parse_head_(::fixy::TextView text) noexcept {
    const std::size_t digit_count = ::fixy::find(text, '\n').value_or(text.size());
    return ::foundation::core::parse_hex<std::uint64_t>(text.window(0, digit_count).value_or(::fixy::TextView{}))
        .ok()
        .map([](std::uint64_t raw) noexcept { return ContentHash{raw}; });
}

// The two members of the resident cache are cold.  A store and a load wait
// on the disk, so the code of the cache needs no speed.  A cold body also
// takes no part of the inline budget of this unit.
[[gnu::cold]] ::fixy::View<const std::byte> Cipher::cached_bytes(ContentHash hash) const noexcept {
    // The walk swaps the hit with each entry after it, so the hit becomes
    // the last entry and the other entries keep their order of use.  No two
    // entries share a hash.  Complexity: linear in the count of entries.
    bool is_hit_found = false;
    for (::fixy::View<CachedObjectBytes, 2> pair : resident_cache_.view().windows<2>()) {
        is_hit_found = is_hit_found || pair.at(::fixy::index<0>()).hash == hash;
        if (is_hit_found) ::fixy::swap(pair.at(::fixy::index<0>()), pair.at(::fixy::index<1>()));
    }
    // The first entry of the reverse walk is the last entry.  It is the hit,
    // or the cache holds no entry of the hash.
    for (const CachedObjectBytes& entry : resident_cache_.view().reverse()) {
        if (entry.hash != hash) break;
        return entry.bytes.view();
    }
    return {};
}

[[gnu::cold]] void Cipher::remember_cached_bytes(ContentHash hash, ::fixy::View<const std::byte> bytes) const {
    if (!hash || bytes.size() == 0 || bytes.size() > MAX_RESIDENT_CACHE_BYTES) {
        return;
    }
    for (const CachedObjectBytes& entry : resident_cache_) {
        if (entry.hash == hash) return;
    }

    // The loop leaves at most MAX_RESIDENT_CACHE_ENTRIES - 1 entries, so
    // the push below stays in the capacity of the cache.  Each pass swaps
    // the least recent entry to the back, one entry at a time, and takes
    // it.  Complexity: linear in the count of entries for each pass.
    while (resident_cache_.size() != 0
           && (resident_cache_.size() >= MAX_RESIDENT_CACHE_ENTRIES
               || resident_cache_bytes_ + bytes.size() > MAX_RESIDENT_CACHE_BYTES)) {
        for (::fixy::View<CachedObjectBytes, 2> pair : resident_cache_.view().windows<2>()) {
            ::fixy::swap(pair.at(::fixy::index<0>()), pair.at(::fixy::index<1>()));
        }
        const CachedObjectBytes evicted = resident_cache_.take_last().expect("the loop runs on a cache with an entry");
        resident_cache_bytes_ -= evicted.bytes.size();
    }

    resident_cache_bytes_ += bytes.size();
    const bool is_full = resident_cache_
                             .append(CachedObjectBytes{
                                 .hash = hash,
                                 .bytes = copy_bytes_(allocation_, bytes),
                             })
                             .is_err();
    if (is_full) [[unlikely]] {
        ::fixy::fatal("the resident cache has no room after its eviction");
    }
}

::fixy::TextView Cipher::open_error_text(OpenError reason) noexcept {
    switch (reason) {
        case OpenError::path_refused:
            return ::fixy::TextView{"the sanitizer refuses the path of the root"};
        case OpenError::missing:
            return ::fixy::TextView{"a part of the root does not exist, and the open cannot make it"};
        case OpenError::not_a_directory:
            return ::fixy::TextView{"a part of the root is no directory, or the root is a link"};
        case OpenError::no_permission:
            return ::fixy::TextView{"the process may not make or open a part of the root"};
        case OpenError::open_failed:
            return ::fixy::TextView{"the make or the open of a part of the root fails"};
        default:
            ::fixy::unreachable();
    }
}

::fixy::TextView Cipher::persist_refusal_text(PersistRefusal reason) noexcept {
    switch (reason) {
        case PersistRefusal::empty_batch:
            return ::fixy::TextView{"the batch holds no event"};
        case PersistRefusal::batch_above_limit:
            return ::fixy::TextView{"the batch holds more events than the limit of a batch"};
        case PersistRefusal::mixed_sessions:
            return ::fixy::TextView{"the events of the batch belong to more than one session"};
        case PersistRefusal::steps_not_consecutive:
            return ::fixy::TextView{"the step ids of the events of the batch are not consecutive"};
        case PersistRefusal::entry_encode_failed:
            return ::fixy::TextView{"the federation entry of the batch does not encode"};
        case PersistRefusal::session_directory_failed:
            return ::fixy::TextView{"the directory of the session does not open, or its sync fails"};
        case PersistRefusal::stored_batch_differs:
            return ::fixy::TextView{"the store holds a batch file of the hash with different bytes"};
        case PersistRefusal::batch_write_failed:
            return ::fixy::TextView{"the write or the flush of the batch file fails"};
        case PersistRefusal::index_append_failed:
            return ::fixy::TextView{"the append or the flush of the index line fails"};
        default:
            ::fixy::unreachable();
    }
}

::fixy::Result<::fixy::Unit, Cipher::OpenError>
Cipher::open_root_(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation,
                   ::fixy::Path<::fixy::tags::source::Sanitized> const& root) {
    // The root opens with O_NOFOLLOW, so a root that is a link gives
    // not_a_directory: the durable-on-return contract needs a root directory
    // whose identity is stable for the life of this instance.
    using Opened = ::fixy::Result<::fixy::fs::DirectoryHandle, OpenError>;
    Opened opened = with_file_ctx_(file_ctx, [&](auto const& ctx) -> Opened {
        CRUCIBLE_TRY(_, make_root_directories_(ctx, root.value().c_text().view()));
        return ::fixy::fs::open_directory(ctx, root).map_err(open_error_of_);
    });
    CRUCIBLE_TRY(root_dir, std::move(opened));
    root_directory_ = std::move(root_dir);
    // The bound of the root is the bound of the path, so the path of the
    // root is a copy of the path.
    root_ = ::fixy::Option<::fixy::Tagged<RootPath, ::fixy::tags::source::Durable>>::some(
        ::fixy::mint_tagged<::fixy::tags::source::Durable>(RootPath{root.value()}));
    // The open makes objects when it is missing and syncs the root, which
    // holds it.  A store opens objects again, so a failure here shows in
    // the first store.
    static_cast<void>(with_file_ctx_(
        file_ctx, [&](auto ctx) { return open_durable_directory_at_(ctx, root_directory_, "objects").is_open(); }));

    load_log(file_ctx, allocation);

    // The log is the record of the commits, so its last commit is the
    // head.  HEAD can lag the log after a failed HEAD write, and it is read
    // only when the log holds no commit.  A missing, long or corrupt HEAD
    // gives no head.
    head_ = latest_committed_head();
    if (head_) {
        return ::fixy::Unit{};
    }
    ::fixy::GrowText head_text = ::fixy::mint_grow_text(MAX_HEAD_BYTES);
    const bool is_head_read = with_file_ctx_(file_ctx, [&](auto ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, root_directory_.ref(), "HEAD", head_text).is_ok();
    });
    const ::fixy::HeapText head = ::fixy::freeze(std::move(head_text));
    head_ = is_head_read ? parse_head_(head.view()).value_or(ContentHash{}) : ContentHash{};
    return ::fixy::Unit{};
}

void Cipher::load_log(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation) {
    // The log has no bound of its own, so the text takes the largest count
    // of a view.  A failed read leaves the bytes that it read in the text,
    // and the parse reads them: a torn last line has no newline, and the
    // loop stops at it.
    ::fixy::GrowText text = ::fixy::mint_grow_text(::fixy::max_of<std::size_t>);
    static_cast<void>(with_file_ctx_(file_ctx, [&](auto ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, root_directory_.ref(), "log", text).is_ok();
    }));
    const ::fixy::HeapText log_text = ::fixy::freeze(std::move(text));
    // Appends the commit of one line that parsed, or skips a corrupt line.
    const auto replay_line = [this, allocation](LogLine const& line) noexcept {
        // The log only grows in step order, and its append checks that
        // order with a contract that aborts.  A line out of order is a
        // corrupt line, so it is skipped like the others.  A step above the
        // ceiling is corrupt too: advance_head refuses such a step, and a run
        // that opens the store starts after the last step.
        if (line.step > MAX_COMMIT_STEP) return;
        if (!log_.empty() && line.step < log_.back().step_id().raw()) return;
        // Each commit takes a larger place than the commit before it, also
        // after a failed append, so a place that does not grow is a corrupt
        // line.  The test keeps the count from going back.
        if (line.order <= commits_.get()) return;

        commits_.advance(line.order);
        log_.append(allocation, SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted,
                                                           ::fixy::session::StepId{line.step},
                                                           ::fixy::session::StateHash{line.hash},
                                                           ::fixy::session::CipherEventOrder{line.order}));
    };
    for (::fixy::TextView rest = log_text.view(); rest.size() != 0;) {
        const LineSplit split = split_line_(rest);
        rest = split.rest;
        // A last line with no newline is a torn record, also when it parses.
        // Its append did not end, so advance_head reported no commit and HEAD
        // did not move.  The next append cuts it.
        if (!split.is_terminated) break;
        for (LogLine const& line : parse_log_line_(split.line))
            replay_line(line);
    }
}

}  // namespace crucible
