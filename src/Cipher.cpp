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

#include <cstring>
#include <span>
#include <vector>

namespace crucible {

namespace {

// The path of an entry under the root of the store, with no leading slash,
// because each file call reads it relative to the descriptor of the root.
// The longest is the temporary file of a session-event batch:
// "session_events/" (15), 16 digits, "/" (1), 16 digits, ".cfed" (5) and
// ".tmp" (4), 57 characters.
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
    ::foundation::core::append_hex<16>(digits, value).expect("sixteen digits fit sixteen characters");
    return digits;
}

// Gives the result of call for the context of the caller.  The context is
// one of three types, so the call compiles one time for each capability.
template <class Call>
[[nodiscard]] auto with_file_ctx_(Cipher::FileCtx const& file_ctx, Call const& call) {
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
[[nodiscard]] bool flush_entry_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::CText path) noexcept {
    return ::fixy::fs::mint_file_at<::fixy::fs::read_only>(ctx, dir, path)
        .match(
            [&ctx](::fixy::Linear<::fixy::fs::OwnedFd> opened) {
                const ::fixy::fs::OwnedFd file = std::move(opened).consume();
                return ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file).is_ok();
            },
            [](::foundation::os::Errno) { return false; });
}

// Atomic file-content replace: write path + ".tmp" under dir, fdatasync
// the bytes, rename onto path, then fsync dir.
//
// A plain truncate-and-write is the alternative and it loses.  The file
// is observably empty between the truncate and the first write, and
// partial between successive writes, so a crash in either window leaves
// it corrupt.  Under replace, the reader sees either the whole old
// content or the whole new content.
//
// renameat without RENAME_NOREPLACE is deliberate.  RENAME_NOREPLACE
// fails when the destination exists, which is the opposite of what a
// replace wants.  Plain renameat is POSIX-atomic for same-filesystem
// replacement.  RENAME_NOREPLACE belongs to content-addressed object
// creation, where a collision means a caller bug or a corrupt
// filesystem.
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
// bytes or a View of bytes, the buffers that write_full takes.
template <class Ctx, class Bytes>
[[nodiscard]] bool replace_entry_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::TextView path,
                                     Bytes bytes) noexcept {
    CRUCIBLE_PRE(::fixy::find(path, '/').is_none());
    EntryPath target{};
    EntryPath temporary{};
    const bool is_named = target.append(path).is_ok() && temporary.append(path).is_ok()
                       && temporary.append(::fixy::TextView{".tmp"}).is_ok();
    if (!is_named) return false;
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const bool is_renamed =
        ::fixy::fs::mint_file_at<WriteTruncate>(ctx, dir, temporary.c_text())
            .match(
                [&](::fixy::Linear<::fixy::fs::OwnedFd> opened) {
                    // The nested scope closes the writer before the rename,
                    // which is what POSIX recommends for atomic-rename writes
                    // and keeps no extra descriptor open across the rename
                    // window.
                    bool is_durable = false;
                    {
                        const ::fixy::fs::OwnedFd file = std::move(opened).consume();
                        is_durable = ::fixy::fs::write_full(ctx, file, bytes).is_ok()
                                  && ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file).is_ok();
                    }
                    // Anchoring both sides of the rename at the same dirfd
                    // makes the swap immune to a concurrent rename of the
                    // parent directory.
                    is_durable = is_durable
                              && ::fixy::fs::commit_atomic_at<::fixy::fs::atomicity::Rename>(
                                     ctx, dir, temporary.c_text(), target.c_text())
                                     .is_ok();
                    if (!is_durable) {
                        static_cast<void>(::fixy::fs::unlink_at(ctx, dir, temporary.c_text()).is_ok());
                    }
                    return is_durable;
                },
                [](::foundation::os::Errno) { return false; });
    // dir is owned by the caller and outlives this call, so the fsync needs
    // no re-open of the parent path.
    return is_renamed && ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir).is_ok();
}

// Atomic append for the monotonically-growing files: the event log and
// the session-event index.  O_APPEND makes the kernel advance the offset
// to end of file before writing, and POSIX makes that write atomic
// within one filesystem block, 4096 bytes on ext4 and XFS.  The records
// are at most 64 and 96 bytes, so they sit inside the window.
//
// The parent-dir fsync only matters for the first record, which creates
// the file.  Later records mutate an existing inode, where fdatasync
// alone would do.
//
// Write-tmp-and-rename is the alternative and it loses here: logs grow
// without bound and a rename does not compose with an append.  If a
// crash tears a record that straddles the block boundary, load_log skips
// the malformed trailing line.  The commit of that record then did not
// happen, and HEAD did not move, because a commit writes HEAD only after
// its log record is durable.
//
// The doors retry EINTR on write, fdatasync and fsync.  Failure needs no
// cleanup, since the file is append-only by construction.  Returns true
// only when both the record bytes and the directory entry are durable.  The
// fsync reaches the directory of the file only when path is one name in
// dir, so a path with a slash ends the process.
template <class Ctx>
[[nodiscard]] bool append_entry_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::CText path,
                                    ::fixy::TextView record) noexcept {
    CRUCIBLE_PRE(::fixy::find(path.view(), '/').is_none());
    using WriteAppend = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteAppend>;
    // With O_APPEND and a record smaller than a block, a write either fails
    // outright or writes every byte.  The door writes again after a short
    // write on a signal, and each write is a fresh kernel-atomic O_APPEND
    // write.
    const bool is_flushed = ::fixy::fs::mint_file_at<WriteAppend>(ctx, dir, path)
                                .match(
                                    [&](::fixy::Linear<::fixy::fs::OwnedFd> opened) {
                                        const ::fixy::fs::OwnedFd file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(ctx, file, record).is_ok()
                                            && ::fixy::fs::sync<::fixy::fs::sync_op::Fdatasync>(ctx, file).is_ok();
                                    },
                                    [](::foundation::os::Errno) { return false; });
    // Makes the file's existence durable for the first record.
    return is_flushed && ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir).is_ok();
}

// Opens the directory at path under dir, and gives a closed Dirfd when it
// does not open.
template <class Ctx>
[[nodiscard]] ::fixy::fs::Dirfd open_level_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::CText path) noexcept {
    return ::fixy::fs::open_dirfd_at(ctx, dir, path)
        .match([](::fixy::fs::Dirfd opened) { return opened; },
               [](::foundation::os::Errno) { return ::fixy::fs::Dirfd{}; });
}

// Makes the directory name under dir when it is missing, syncs dir, and
// opens the directory.  A new directory is a new entry of dir, and the sync
// of dir makes that entry durable.  The sync runs also when the directory
// is there, because the process that made it can have ended before its own
// sync.  A closed Dirfd stands for a directory that does not open, or for a
// sync that fails.  The store owns each directory that it opens here.
template <class Ctx>
[[nodiscard]] ::fixy::fs::Dirfd open_durable_directory_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir,
                                                           ::fixy::CText name) noexcept {
    static_cast<void>(::fixy::fs::make_directory_at(ctx, dir, name).is_ok());
    if (::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir).is_err()) return ::fixy::fs::Dirfd{};
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
[[nodiscard]] ::fixy::Result<::fixy::fs::Dirfd, Cipher::OpenError>
open_prefix_(Ctx const& ctx, Cipher::RootPath const& prefix) noexcept {
    using Opened = ::fixy::Result<::fixy::fs::Dirfd, Cipher::OpenError>;
    return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(prefix.c_text().view()))
        .match(
            [&](::fixy::Path<::fixy::tags::source::Sanitized> path) -> Opened {
                return ::fixy::fs::open_dirfd(ctx, path).match(
                    [](::fixy::fs::Dirfd dir) -> Opened { return dir; },
                    [](::foundation::os::Errno error) -> Opened { return ::fixy::err(open_error_of_(error)); });
            },
            [](::fixy::PathTraversalError) -> Opened { return ::fixy::err(Cipher::OpenError::path_refused); });
}

// Makes the directory name under dir, and syncs dir, which holds the new
// entry.  A name that is there gives eexist, and the make then gives no
// error, so the open after it gives the reason of a name that is no
// directory.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, Cipher::OpenError> make_part_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir,
                                                                         ::fixy::CText name) noexcept {
    using Made = ::fixy::Result<::fixy::Unit, Cipher::OpenError>;
    return ::fixy::fs::make_directory_at(ctx, dir, name)
        .match(
            [&](::fixy::Unit) -> Made {
                if (::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, dir).is_err()) {
                    return ::fixy::err(Cipher::OpenError::open_failed);
                }
                return ::fixy::Unit{};
            },
            [](::foundation::os::Errno error) -> Made {
                if (error == ::foundation::os::Errno::eexist) return ::fixy::Unit{};
                return ::fixy::err(open_error_of_(error));
            });
}

// True when opened holds the error reason.
[[nodiscard]] bool holds_error_(::fixy::Result<::fixy::fs::Dirfd, Cipher::OpenError> const& opened,
                                Cipher::OpenError reason) noexcept {
    return opened.match([](::fixy::fs::Dirfd const&) { return false; },
                        [reason](Cipher::OpenError given) { return given == reason; });
}

// Opens the directory at prefix as open_prefix_ does, but a directory that
// opens only for search gives a closed Dirfd.  The open of the root needs
// search permission on each parent and read permission on the root alone,
// so the walk goes on past such a parent.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::fs::Dirfd, Cipher::OpenError>
open_searchable_(Ctx const& ctx, Cipher::RootPath const& prefix) noexcept {
    ::fixy::Result<::fixy::fs::Dirfd, Cipher::OpenError> opened = open_prefix_(ctx, prefix);
    if (holds_error_(opened, Cipher::OpenError::no_permission)) return ::fixy::fs::Dirfd{};
    return opened;
}

// Opens the part at prefix, and makes it under parent when it is missing.
// A missing part under a parent that opens only for search gives
// no_permission, because the make needs the Dirfd of the parent.
template <class Ctx>
[[nodiscard]] ::fixy::Result<::fixy::fs::Dirfd, Cipher::OpenError>
open_part_(Ctx const& ctx, ::fixy::fs::Dirfd const& parent, Cipher::RootPath const& prefix,
           ::fixy::CText name) noexcept {
    using OpenError = Cipher::OpenError;
    ::fixy::Result<::fixy::fs::Dirfd, OpenError> opened = open_searchable_(ctx, prefix);
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
// missing part under the Dirfd of the part before it, and it gives a closed
// Dirfd for a part that opens only for search.  The open of the root after
// the walk gives the reason of a root that does not open.  The sanitizer of
// the root refused each ".." part.  Each name below the root still opens
// beneath the Dirfd of the root.  The prefix adds "./" and a slash to the
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
    if (prefix.append(start).is_err()) return ::fixy::err(OpenError::path_refused);
    CRUCIBLE_TRY(start_dir, open_searchable_(ctx, prefix));
    ::fixy::fs::Dirfd level = std::move(start_dir);
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
                    [&level](::fixy::fs::Dirfd next_level) -> ::fixy::Option<OpenError> {
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
read_batch_at_(Ctx const& ctx, ::foundation::effects::Alloc allocation, ::fixy::fs::Dirfd const& dir,
               ::fixy::CText path, std::size_t maximum) noexcept {
    // The rvalue match refuses a result that holds an address, and a
    // HeapArray holds one.  So the arms give the reason or no reason, and
    // the bytes leave through the Option.
    ::fixy::Option<::fixy::HeapArray<std::byte>> read{};
    const ::fixy::Option<LoadError> refusal =
        ::fixy::fs::mint_file_at<::fixy::fs::read_only>(ctx, dir, path)
            .match(
                [&](::fixy::Linear<::fixy::fs::OwnedFd> opened) -> ::fixy::Option<LoadError> {
                    const ::fixy::fs::OwnedFd file = std::move(opened).consume();
                    const std::int64_t size = ::fixy::fs::file_size(ctx, file).value_or(0);
                    if (size <= 0 || static_cast<std::uint64_t>(size) > maximum) {
                        return ::fixy::some(LoadError::file_size);
                    }
                    ::fixy::HeapArray<std::byte> bytes =
                        ::fixy::mint_heap_array(allocation, static_cast<std::size_t>(size), std::byte{0});
                    if (::fixy::fs::read_full(ctx, file, bytes.view()).value_or(0) != bytes.size()) {
                        return ::fixy::some(LoadError::file_size);
                    }
                    read = ::fixy::Option<::fixy::HeapArray<std::byte>>::some(std::move(bytes));
                    return ::fixy::none;
                },
                [](::foundation::os::Errno) -> ::fixy::Option<LoadError> {
                    return ::fixy::some(LoadError::file_open_failed);
                });
    for (const LoadError reason : refusal)
        return ::fixy::err(reason);
    return std::move(read).expect("a file with no refusal gave its bytes");
}

// An owned copy of bytes in new storage of allocation.  Complexity: one
// allocation and a copy of each byte.
[[nodiscard]] ::fixy::HeapArray<std::byte> copy_bytes_(::foundation::effects::Alloc allocation,
                                                       ::fixy::View<const std::byte> bytes) noexcept {
    ::fixy::HeapArray<std::byte> owned = ::fixy::mint_heap_array(allocation, bytes.size(), std::byte{0});
    ::fixy::copy(owned.view(), bytes).expect("the copy has the count of the bytes");
    return owned;
}

// The first line of a text, without its newline, and the text after the
// newline.  A text with no newline is one line.
struct LineSplit {
    ::fixy::TextView line{};
    ::fixy::TextView rest{};
};

[[nodiscard]] LineSplit split_line_(::fixy::TextView text) noexcept {
    const std::size_t length = ::fixy::find(text, '\n').value_or(text.size());
    const std::size_t next = ::fixy::min(length + 1, text.size());
    return LineSplit{
        .line = text.window(0, length).value_or(::fixy::TextView{}),
        .rest = text.window(next, text.size() - next).value_or(::fixy::TextView{}),
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

// One line of the head log: "<step>,<16 hexadecimal digits>,<ts>".
struct LogLine {
    std::uint64_t step = 0;
    std::uint64_t hash = 0;
    std::uint64_t committed_at = 0;
};

[[nodiscard]] ::fixy::Option<LogLine> parse_log_line_(::fixy::TextView line) noexcept {
    CRUCIBLE_TRY(first_split, split_at_comma_(line));
    CRUCIBLE_TRY(hash_split, split_at_comma_(first_split.tail));
    CRUCIBLE_TRY(step, parse_decimal_(first_split.head));
    CRUCIBLE_TRY(hash, parse_hex_field_(hash_split.head));
    CRUCIBLE_TRY(committed_at, parse_decimal_(hash_split.tail));
    return ::fixy::some(LogLine{.step = step, .hash = hash, .committed_at = committed_at});
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
// The three unconditional terms sum to 352 bytes, so the result is
// positive by construction.  Stating that in the return type means a
// refactor that drops the headroom or makes a header conditional trips
// the mint's precondition here, instead of silently handing the caller
// a zero-length buffer to serialize into.
::fixy::Positive<std::size_t> estimate_serial_size(RegionNode const& region) {
    size_t sz = 64;  // header
    sz += 32;  // region fixed fields
    for (const ::fixy::Ref<MemoryPlan>& plan : region.plan) {
        sz += 64 + plan->num_slots() * sizeof(TensorSlot);
    }
    for (const TraceEntry& te : region.ops) {
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

ContentHash Cipher::store_(FileCtx const& file_ctx, OpenView const&, ContentAddressedRegionPayload payload) {
    ContentAddressedRegionPayload::Value value = payload.get();
    if (value.is_none()) return ContentHash{};
    const RegionNode& region = *std::move(value).expect("the payload holds a region");
    const ContentHash hash = region.content_hash;
    if (!hash) return ContentHash{};

    // The object is objects/<shard>/<leaf>.  The shard directory holds the
    // objects of one first byte of the hash, and it holds the rename of the
    // object, so its sync makes the object durable.  The open makes the
    // shard when it is missing and syncs objects, which holds the shard.
    const EntryPath shard = object_shard_(hash.raw());
    const EntryPath leaf = object_leaf_(hash.raw());
    const ::fixy::fs::Dirfd shard_dir = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return open_durable_directory_at_(ctx, open_level_(ctx, root_dirfd_, "objects"), shard.c_text());
    });
    if (!shard_dir.is_open()) return ContentHash{};

    // The skip path also flushes the bytes and the entry of the object.
    // Another process may have written them and died before the kernel
    // flushed them, and this process is about to return the hash as
    // durable.
    const bool is_present = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::has_entry_at(ctx, shard_dir, leaf.c_text()).value_or(false);
    });
    if (is_present) {
        const bool is_flushed = with_file_ctx_(file_ctx, [&](auto const& ctx) {
            return flush_entry_at_(ctx, shard_dir, leaf.c_text())
                && ::fixy::fs::sync<::fixy::fs::sync_op::FsyncParentDir>(ctx, shard_dir).is_ok();
        });
        return is_flushed ? hash : ContentHash{};
    }

    const size_t cap = estimate_serial_size(region).value();
    ::fixy::HeapArray<std::byte> image = ::fixy::mint_heap_array(allocation_, cap, std::byte{0});
    const size_t n = serialize_region(SerializedRegion{region}, image.view());
    if (n == 0) return ContentHash{};
    const ::fixy::View<const std::byte> written =
        image.view().window(0, n).expect("the writer writes inside its image");

    // The replace writes a temporary file in the shard, flushes its bytes,
    // renames it onto the object, and syncs the shard.  A reader then sees
    // the whole object or no object, and a hash that this function returns
    // is loadable after a crash.
    const bool is_written = with_file_ctx_(
        file_ctx, [&](auto const& ctx) { return replace_entry_at_(ctx, shard_dir, leaf.c_text().view(), written); });
    if (!is_written) {
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
                                                   ::fixy::BumpArena& arena) const {
    if (!content_hash) return ::fixy::err(LoadError::none_hash);

    // A cached image that gives no region of the hash falls through to the
    // file, and the file gives the reason of a refusal.
    const ::fixy::View<const std::byte> cached = cached_bytes(content_hash);
    if (cached.size() != 0) {
        for (const LoadedRegionNode& loaded_region : deserialize_region(a, cached, arena).ok()) {
            if (loaded_region.value()->content_hash == content_hash) {
                return LoadedContentAddressedRegionPayload{::fixy::to(*loaded_region.value()), true};
            }
        }
    }

    // The read refuses a missing object, an empty object and an object
    // past MAX_OBJECT_BYTES, and the mint states the same ceiling again.
    // If the read is later changed and drops its bound, the mint's
    // precondition still fires.
    const EntryPath object = object_entry_(content_hash.raw());
    ::fixy::Result<::fixy::HeapArray<std::byte>, LoadError> read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return read_batch_at_(ctx, a, root_dirfd_, object.c_text(), MAX_OBJECT_BYTES);
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
    return LoadedContentAddressedRegionPayload{::fixy::to(*loaded_region.value()), false};
}

ContentHash Cipher::persist_session_events_(FileCtx const& file_ctx, OpenView const&,
                                            ::foundation::effects::Alloc allocation,
                                            ::fixy::View<SessionEvent const> events) {
    if (events.size() == 0) return ContentHash{};

    if (events.size() > MAX_SESSION_EVENT_BATCH_EVENTS) {
        return ContentHash{};
    }

    const ::fixy::session::SessionTagId session = events.get(0).expect("the batch holds an event")->session();
    const uint64_t first_step = events.get(0).expect("the batch holds an event")->step_id().value;
    const uint64_t last_step = events.get(events.size() - 1).expect("the batch holds an event")->step_id().value;
    bool has_previous = false;
    uint64_t previous_step = 0;
    for (const SessionEvent& event : events) {
        if (event.session() != session) [[unlikely]] {
            return ContentHash{};
        }
        const uint64_t step = event.step_id().value;
        if (has_previous && (previous_step == ::fixy::max_of<uint64_t> || previous_step + 1 != step)) [[unlikely]] {
            return ContentHash{};
        }
        previous_step = step;
        has_previous = true;
    }

    const std::size_t payload_bytes = events.size() * sizeof(SessionEvent);
    if (payload_bytes > MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
        return ContentHash{};
    }

    // Each event goes to the batch through its own encoder, which is
    // the one route from an event to bytes.  The hash consumes those
    // bytes structurally.  Complexity: linear in the number of events.
    ::fixy::HeapArray<std::byte> payload = ::fixy::mint_heap_array(allocation, payload_bytes, std::byte{0});
    std::size_t position = 0;
    for (const SessionEvent& event : events) {
        const auto record = event.encode();
        ::fixy::copy(payload.view()
                         .window<sizeof(SessionEvent)>(position * sizeof(SessionEvent))
                         .expect("the payload holds a record for each event"),
                     ::fixy::bytes(record));
        ++position;
    }
    const ContentHash hash = session_event_batch_hash(payload.view());
    const KernelCacheKey key{hash, SESSION_EVENT_FEDERATION_ROW_HASH};

    // The entry fills its buffer: the header and then the payload.
    ::fixy::HeapArray<std::byte> encoded =
        ::fixy::mint_heap_array(allocation, cipher::federation::FEDERATION_HEADER_BYTES + payload_bytes, std::byte{0});
    if (cipher::federation::serialize_federation_entry(encoded.view(), key, payload.view()).is_err()) {
        return ContentHash{};
    }

    // The index line: "<first_step>,<last_step>,<count>,<16-hex hash>\n".
    // The longest form is 20 + 1 + 20 + 1 + 20 + 1 + 16 + 1 = 80 bytes,
    // well under the 4096-byte window in which POSIX makes a single
    // O_APPEND write to a regular file atomic.
    //
    // load_session_events tolerates a malformed trailing line, so a
    // crash mid-record drops one index entry.  The batch file itself is
    // already on disk, and the next persist_session_events call
    // re-appends the entry.
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
    // same bytes, and the replace makes a new batch file durable before the
    // index names it.
    const bool is_indexed = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        const ::fixy::fs::Dirfd session_dir = open_durable_directory_at_(
            ctx, open_durable_directory_at_(ctx, root_dirfd_, "session_events"), session_name.c_text());
        if (!session_dir.is_open()) return false;
        const bool is_batch_present = ::fixy::fs::has_entry_at(ctx, session_dir, batch.c_text()).value_or(false);
        const bool is_batch_stored = is_batch_present
                                       ? read_batch_at_(ctx, allocation, session_dir, batch.c_text(), encoded.size())
                                             .match(
                                                 [&encoded](::fixy::HeapArray<std::byte> stored) {
                                                     return ::fixy::equal(stored.view(), encoded.view());
                                                 },
                                                 [](LoadError) { return false; })
                                       : replace_entry_at_(ctx, session_dir, batch.c_text().view(),
                                                           ::fixy::View<std::byte const>{encoded.view()});
        return is_batch_stored && append_entry_at_(ctx, session_dir, "index", index_line);
    });
    if (!is_indexed) {
        return ContentHash{};
    }
    return hash;
}

::foundation::core::GrowList<Cipher::SessionEvent>
Cipher::load_session_events_(FileCtx const& file_ctx, OpenView const&, ::foundation::effects::Alloc allocation,
                             ::fixy::session::SessionTagId session, ::fixy::session::StepId from_step) const {
    // The index bounds no count of events, so the list takes the largest
    // count of a View.
    ::foundation::core::GrowList<SessionEvent> out =
        ::foundation::core::mint_grow_list<SessionEvent>(::fixy::max_of<std::size_t>);
    const EntryPath session_directory = session_entry_(session);
    auto opened = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::open_dirfd_at(ctx, root_dirfd_, session_directory.c_text());
    });
    if (opened.is_err()) return out;
    const ::fixy::fs::Dirfd session_dir = std::move(opened).expect("the session directory opened");

    // The index has no bound of its own, so the text takes the largest
    // count of a view.  A failed read leaves the bytes that it read in the
    // text, and the parse reads them: a torn last line fails its parse.
    ::fixy::GrowText index_text = ::fixy::mint_grow_text(::fixy::max_of<std::size_t>);
    static_cast<void>(with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, session_dir, "index", index_text).is_ok();
    }));

    uint64_t highest_loaded = from_step.value;
    bool have_loaded = false;
    for (::fixy::TextView rest = index_text.view(); rest.size() != 0;) {
        const LineSplit split = split_line_(rest);
        rest = split.rest;
        ::fixy::Option<IndexLine> parsed = parse_index_line_(split.line);
        if (parsed.is_none()) continue;
        const IndexLine index_line = std::move(parsed).expect("the index line parsed");
        const uint64_t first = index_line.first;
        const uint64_t last = index_line.last;
        const uint64_t count = index_line.count;
        if (last < from_step.value) continue;
        if (count == 0 || count > MAX_SESSION_EVENT_BATCH_EVENTS) continue;
        if ((last - first + 1u) != count) continue;

        const ContentHash hash{index_line.hash};
        const EntryPath batch = batch_entry_(index_line.hash);
        // A batch that does not read is skipped, whatever the reason.
        ::fixy::Result<::fixy::HeapArray<std::byte>, LoadError> read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
            return read_batch_at_(ctx, allocation, session_dir, batch.c_text(),
                                  cipher::federation::FEDERATION_HEADER_BYTES + MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES);
        });
        if (read.is_err()) continue;
        const ::fixy::HeapArray<std::byte> bytes = std::move(read).expect("the batch file read");

        auto entry = cipher::federation::deserialize_untrusted_federation_entry(
            bytes.view(), cipher::federation::FEDERATION_UNIVERSE_CARDINALITY);
        if (entry.is_err()) continue;
        const cipher::federation::FederationEntryView view = std::move(entry).expect("the entry decoded");
        if (view.header.content_hash != hash) continue;
        if (view.header.row_hash != SESSION_EVENT_FEDERATION_ROW_HASH) {
            continue;
        }
        if (session_event_batch_hash(view.payload) != hash) continue;
        if ((view.payload.size() % sizeof(SessionEvent)) != 0u) continue;

        const std::size_t n = view.payload.size() / sizeof(SessionEvent);
        if (n != count) continue;

        // The hashes prove only that the batch is the one its writer
        // stored, and anyone who writes the store can compute them.  So
        // each record goes through the event decoder, which is the one
        // route from bytes to an event and refuses a record with a byte
        // outside its field.
        auto decoded = ::fixy::session::decode_session_log(allocation, view.payload);
        if (decoded.is_err()) continue;
        const ::fixy::HeapArray<SessionEvent> events = std::move(decoded).expect("the batch decoded");
        // The count check above gives the batch count events, and count is
        // above zero.
        if (events.view().get(0).expect("the batch holds an event")->step_id().value != first) continue;
        if (events.view().get(events.size() - 1).expect("the batch holds an event")->step_id().value != last) {
            continue;
        }
        bool is_valid_batch = true;
        bool has_previous = false;
        uint64_t previous_step = 0;
        for (const SessionEvent& event : events.view()) {
            const uint64_t step = event.step_id().value;
            const bool is_out_of_session = event.session() != session;
            const bool is_out_of_sequence =
                has_previous && (previous_step == ::fixy::max_of<uint64_t> || previous_step + 1u != step);
            const bool is_loaded_already = have_loaded && step <= highest_loaded;
            if (is_out_of_session || is_out_of_sequence || is_loaded_already) {
                is_valid_batch = false;
                break;
            }
            previous_step = step;
            has_previous = true;
        }
        if (!is_valid_batch) continue;

        // The steps of the batch are first, first + 1, ..., last, and
        // last is at or after from_step.  So the events at from_step or
        // after are one suffix of the batch, and it is not empty.
        const std::uint64_t skipped = from_step.value > first ? from_step.value - first : std::uint64_t{0};
        const ::fixy::View<SessionEvent const> suffix =
            events.view().window(skipped, events.size() - skipped).expect("the suffix is inside the batch");
        for (const SessionEvent& event : suffix) {
            out.append(allocation, event).expect("the list of the events has no maximum");
        }
        highest_loaded = last;
        have_loaded = true;
    }
    return out;
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
        const std::size_t mid = lo + (hi - lo) / 2;
        if (log_[mid].step_id().value <= step_id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    while (lo > 0) {
        --lo;
        const LogEntry& entry = log_[lo];
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
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    if (h == 0u) h = 0x53455353494f4e45ULL;
    if (h == ::fixy::max_of<uint64_t>) --h;
    return ContentHash{h};
}

ContentHash Cipher::latest_committed_head() const noexcept {
    std::size_t i = log_.size();
    while (i > 0) {
        --i;
        const LogEntry& entry = log_[i];
        if (commits_head_(entry)) {
            return committed_hash_(entry);
        }
    }
    return ContentHash{};
}

Cipher::CommitResult Cipher::advance_head(FileCtx const& file_ctx, OpenView const&, ContentHash content_hash,
                                          uint64_t step_id, ::fixy::MonotonicClockBytes<std::uint64_t> committed_at) {
    // Weakly increasing, not strictly: duplicate step ids are accepted
    // at this gate.  The strict consecutive-by-one check belongs to the
    // session-event batch invariant, not to the head log.
    if (!log_.empty()) {
        CRUCIBLE_PRE(log_.back().step_id().value <= step_id);
    }
    const std::uint64_t committed_at_ns = committed_at.peek();

    // The log record is the commit, and it is durable before HEAD moves.
    // A failed append changes no state in memory, so this store and a
    // reopen agree.  A write that passed before a failed flush can leave
    // the record in the file, and a reopen then reads that commit.
    //
    // Record layout: "<step_id>,<16-hex content_hash>,<ts>\n".  The
    // longest form is 20 decimal digits + 1 + 16 + 1 + 20 decimal digits
    // + 1 = 59 bytes, so 64 bytes suffices.  That is well under the
    // 4096-byte window in which POSIX makes a single O_APPEND write to a
    // regular file atomic.
    const ::fixy::FixedText<16> hash_digits = hex_digits_(content_hash.raw());
    ::fixy::FixedText<64> record{};
    const ::fixy::TextView log_line = ::fixy::format(record, "{},{},{}\n", step_id, hash_digits.view(), committed_at_ns)
                                          .expect("a log record fits 64 characters");
    const bool is_appended =
        with_file_ctx_(file_ctx, [&](auto const& ctx) { return append_entry_at_(ctx, root_dirfd_, "log", log_line); });
    if (!is_appended) {
        return ::fixy::err(CommitError::log_append);
    }
    log_.append(SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted, ::fixy::session::StepId{step_id},
                                           ::fixy::session::StateHash{content_hash.raw()}, committed_at_ns));
    head_ = content_hash;

    // HEAD is a copy of the last commit of the log.  A failed write leaves
    // HEAD at an earlier commit, and the open takes the head from the log.
    ::fixy::FixedText<17> head_line{};
    head_line.append(hash_digits.view()).expect("sixteen digits fit the HEAD line");
    head_line.push('\n').expect("the newline fits the HEAD line");
    const bool is_head_written = with_file_ctx_(
        file_ctx, [&](auto const& ctx) { return replace_entry_at_(ctx, root_dirfd_, "HEAD", head_line.view()); });

    CRUCIBLE_POST(0, head_ == content_hash);
    CRUCIBLE_POST(0, !log_.empty());
    CRUCIBLE_POST(0, log_.back().step_id().value == step_id);
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
        .match([](std::uint64_t raw) -> ::fixy::Option<ContentHash> { return ::fixy::some(ContentHash{raw}); },
               [](::fixy::ParseError) -> ::fixy::Option<ContentHash> { return ::fixy::none; });
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
                             .push(CachedObjectBytes{
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

::fixy::Result<::fixy::Unit, Cipher::OpenError>
Cipher::open_root_(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation,
                   ::fixy::Path<::fixy::tags::source::Sanitized> const& root) {
    // The root opens with O_NOFOLLOW, so a root that is a link gives
    // not_a_directory: the durable-on-return contract needs a root directory
    // whose identity is stable for the life of this instance.
    using Opened = ::fixy::Result<::fixy::fs::Dirfd, OpenError>;
    Opened opened = with_file_ctx_(file_ctx, [&](auto const& ctx) -> Opened {
        CRUCIBLE_TRY(_, make_root_directories_(ctx, root.value().c_text().view()));
        return ::fixy::fs::open_dirfd(ctx, root).match(
            [](::fixy::fs::Dirfd dir) -> Opened { return dir; },
            [](::foundation::os::Errno error) -> Opened { return ::fixy::err(open_error_of_(error)); });
    });
    CRUCIBLE_TRY(root_dir, std::move(opened));
    root_dirfd_ = std::move(root_dir);
    // The bound of the root is the bound of the path, so the path of the
    // root is a copy of the path.
    [[maybe_unused]] const bool root_set =
        root_.try_set(::fixy::mint_tagged<::fixy::tags::source::Durable>(RootPath{root.value()}));
    [[assume(root_set)]];
    // The open makes objects when it is missing and syncs the root, which
    // holds it.  A store opens objects again, so a failure here shows in
    // the first store.
    static_cast<void>(with_file_ctx_(
        file_ctx, [&](auto const& ctx) { return open_durable_directory_at_(ctx, root_dirfd_, "objects").is_open(); }));

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
    const bool is_head_read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, root_dirfd_, "HEAD", head_text).is_ok();
    });
    head_ = is_head_read ? parse_head_(head_text.view()).value_or(ContentHash{}) : ContentHash{};
    return ::fixy::Unit{};
}

void Cipher::load_log(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation) {
    // The log has no bound of its own, so the text takes the largest count
    // of a view.  A failed read leaves the bytes that it read in the text,
    // and the parse reads them: a torn last line fails its parse.
    ::fixy::GrowText text = ::fixy::mint_grow_text(::fixy::max_of<std::size_t>);
    static_cast<void>(with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, root_dirfd_, "log", text).is_ok();
    }));
    for (::fixy::TextView rest = text.view(); rest.size() != 0;) {
        const LineSplit split = split_line_(rest);
        rest = split.rest;
        ::fixy::Option<LogLine> parsed = parse_log_line_(split.line);
        if (parsed.is_none()) continue;
        const LogLine line = std::move(parsed).expect("the log line parsed");
        // The log only grows in step order, and its append checks that
        // order with a contract that aborts.  A line out of order is a
        // corrupt line, so it is skipped like the others.
        if (!log_.empty() && line.step < log_.back().step_id().value) continue;

        log_.append(SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted,
                                               ::fixy::session::StepId{line.step},
                                               ::fixy::session::StateHash{line.hash}, line.committed_at));
    }
}

}  // namespace crucible
