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

// The entry of the object of hash: "objects/<2 digits>/<14 digits>", the
// sixteen hexadecimal digits of the hash.
[[nodiscard]] EntryPath object_entry_(std::uint64_t hash) noexcept {
    EntryPath path{};
    path.append(::fixy::TextView{"objects/"}).expect("the object entry fits its path");
    path.append_hex<2>(hash >> 56U).expect("the object entry fits its path");
    path.append('/').expect("the object entry fits its path");
    path.append_hex<14>(hash).expect("the object entry fits its path");
    return path;
}

// The entry of the directory of the events of a session:
// "session_events/<16 digits>".
[[nodiscard]] EntryPath session_entry_(::fixy::session::SessionTagId session) noexcept {
    EntryPath path{};
    path.append(::fixy::TextView{"session_events/"}).expect("the session entry fits its path");
    path.append_hex<16>(session.value).expect("the session entry fits its path");
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
// and the new bytes would be orphaned in the tmp file.
//
// The doors retry EINTR on write, fdatasync, renameat and fsync.  If any
// step fails after the tmp file exists, it is unlinked so a later run does
// not trip over a partial-write artifact.  Bytes is a TextView or a span of
// bytes, the two buffers that write_full takes.
template <class Ctx, class Bytes>
[[nodiscard]] bool replace_entry_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::TextView path,
                                     Bytes bytes) noexcept {
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
// only when both the record bytes and the directory entry are durable.
template <class Ctx>
[[nodiscard]] bool append_entry_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::CText path,
                                    ::fixy::TextView record) noexcept {
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

// Makes each directory of root that is missing, from the first component
// on, as mkdir -p does.  here is the current directory, which anchors a
// relative root, and an absolute root does not read it.  A directory that
// is there gives eexist, and a directory that the make cannot make fails
// the open of the root after it.  Complexity: one make for each component
// of the root, and one copy of each character.
template <class Ctx>
void make_root_directories_(Ctx const& ctx, ::fixy::fs::Dirfd const& here, ::fixy::TextView root) noexcept {
    Cipher::RootPath prefix{};
    for (const char character : root) {
        if (character == '/' && prefix.size() != 0) {
            static_cast<void>(::fixy::fs::make_directory_at(ctx, here, prefix.c_text()).is_ok());
        }
        static_cast<void>(prefix.append(character).is_ok());
    }
    static_cast<void>(::fixy::fs::make_directory_at(ctx, here, prefix.c_text()).is_ok());
}

// Reads each byte of path under dir into bytes, when the file holds more
// than zero and at most maximum bytes.  bytes takes the size of the file.
// Gives false for a file that does not open, has another size, or reads
// short.
template <class Ctx>
[[nodiscard]] bool read_entry_at_(Ctx const& ctx, ::fixy::fs::Dirfd const& dir, ::fixy::CText path, std::size_t maximum,
                                  std::vector<std::uint8_t>& bytes) noexcept {
    return ::fixy::fs::mint_file_at<::fixy::fs::read_only>(ctx, dir, path)
        .match(
            [&](::fixy::Linear<::fixy::fs::OwnedFd> opened) {
                const ::fixy::fs::OwnedFd file = std::move(opened).consume();
                const std::int64_t size = ::fixy::fs::file_size(ctx, file).value_or(0);
                if (size <= 0 || static_cast<std::uint64_t>(size) > maximum) return false;
                bytes.resize(static_cast<std::size_t>(size));
                return ::fixy::fs::read_full(ctx, file, std::as_writable_bytes(std::span<std::uint8_t>{bytes}))
                           .value_or(0)
                    == bytes.size();
            },
            [](::foundation::os::Errno) { return false; });
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
    if (region.plan) {
        sz += 64 + region.plan->num_slots() * sizeof(TensorSlot);
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
    const RegionNode* region = payload.get();
    if (!region) return ContentHash{};
    const ContentHash hash = region->content_hash;
    if (!hash) return ContentHash{};

    const EntryPath object = object_entry_(hash.raw());

    // The skip path also fdatasyncs.  Another process may have written
    // these bytes and died before the kernel flushed them, and this
    // process is about to return the hash as durable.
    const bool is_present = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::has_entry_at(ctx, root_dirfd_, object.c_text()).value_or(false);
    });
    if (is_present) {
        const bool is_flushed = with_file_ctx_(
            file_ctx, [&](auto const& ctx) { return flush_entry_at_(ctx, root_dirfd_, object.c_text()); });
        return is_flushed ? hash : ContentHash{};
    }

    const size_t cap = estimate_serial_size(*region).value();
    std::vector<uint8_t> buf(cap);
    const size_t n = serialize_region(SerializedRegion{*region}, SerialBuffer{buf});
    if (n == 0) return ContentHash{};

    // The directory of the object holds the objects of one first byte of
    // the hash.  A directory that is there gives eexist, and a directory
    // that the make cannot make fails the write below.
    EntryPath directory{};
    directory.append(::fixy::TextView{"objects/"}).expect("the object directory fits its path");
    directory.append_hex<2>(hash.raw() >> 56U).expect("the object directory fits its path");

    // The replace writes a temporary file, flushes its bytes, and renames
    // it onto the object.  A reader then sees the whole object or no
    // object, and a hash that this function returns is loadable after a
    // crash.
    const bool is_written = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        static_cast<void>(::fixy::fs::make_directory_at(ctx, root_dirfd_, directory.c_text()).is_ok());
        return replace_entry_at_(ctx, root_dirfd_, object.c_text().view(),
                                 std::as_bytes(std::span<const uint8_t>{buf.data(), n}));
    });
    if (!is_written) {
        return ContentHash{};
    }

    remember_cached_bytes(hash, std::span<const uint8_t>{buf.data(), n});
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

Cipher::LoadedContentAddressedRegionPayload Cipher::load_content_addressed_(FileCtx const& file_ctx, OpenView const&,
                                                                            ::foundation::effects::Alloc a,
                                                                            ContentHash content_hash,
                                                                            ::fixy::BumpArena& arena) const {
    if (!content_hash) return nullptr;

    const std::span<const uint8_t> cached = cached_bytes(content_hash);
    if (!cached.empty()) {
        const std::optional<LoadedRegionNode> loaded_region = deserialize_region(a, cached, arena);
        if (loaded_region && loaded_region->value()->content_hash == content_hash) {
            return LoadedContentAddressedRegionPayload{loaded_region->value(), true};
        }
    }

    // The read refuses a missing object, an empty object and an object
    // past MAX_OBJECT_BYTES, and the mint states the same ceiling again.
    // If the read is later changed and drops its bound, the mint's
    // precondition still fires.
    const EntryPath object = object_entry_(content_hash.raw());
    std::vector<uint8_t> buf;
    const bool is_read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return read_entry_at_(ctx, root_dirfd_, object.c_text(), MAX_OBJECT_BYTES, buf);
    });
    if (!is_read) return nullptr;
    [[maybe_unused]] const ValidatedObjectSize validated_len =
        ::fixy::mint_refined<::fixy::bounded_above<MAX_OBJECT_BYTES>>(buf.size());

    const std::optional<LoadedRegionNode> loaded_region = deserialize_region(a, std::span<const uint8_t>{buf}, arena);
    // The file name is the key, and anyone who writes the store can put
    // any region under any name.  A region whose own hash is not the key
    // is refused, the same as on the cache path above, so a lookup never
    // returns another region.
    if (!loaded_region || loaded_region->value()->content_hash != content_hash) return nullptr;
    remember_cached_bytes(content_hash, std::span<const uint8_t>{buf});
    return LoadedContentAddressedRegionPayload{loaded_region->value(), false};
}

ContentHash Cipher::persist_session_events_(FileCtx const& file_ctx, OpenView const&,
                                            std::span<const SessionEvent> events) {
    if (events.empty()) return ContentHash{};

    if (events.size() > MAX_SESSION_EVENT_BATCH_EVENTS) {
        return ContentHash{};
    }

    const ::fixy::session::SessionTagId session = events.front().session();
    for (std::size_t i = 1; i < events.size(); ++i) {
        if (events[i].session() != session) [[unlikely]] {
            return ContentHash{};
        }
        const uint64_t prev_step = events[i - 1].step_id().value;
        if (prev_step == ::fixy::max_of<uint64_t> || prev_step + 1 != events[i].step_id().value) [[unlikely]] {
            return ContentHash{};
        }
    }

    const std::size_t payload_bytes = events.size() * sizeof(SessionEvent);
    if (payload_bytes > MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
        return ContentHash{};
    }

    // Each event goes to the batch through its own encoder, which is
    // the one route from an event to bytes.  The hash consumes those
    // bytes structurally.  Complexity: linear in the number of events.
    std::vector<std::uint8_t> payload(payload_bytes);
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto record = events[i].encode();
        std::memcpy(payload.data() + i * sizeof(SessionEvent), record.data(), record.size());
    }
    const ContentHash hash = session_event_batch_hash(std::span<const std::uint8_t>{payload});
    const KernelCacheKey key{hash, SESSION_EVENT_FEDERATION_ROW_HASH};

    std::vector<std::uint8_t> encoded(cipher::federation::FEDERATION_HEADER_BYTES + payload.size());
    auto written = cipher::federation::serialize_federation_entry(std::span<std::uint8_t>{encoded}, key,
                                                                  std::span<const std::uint8_t>{payload});
    if (written.is_err()) return ContentHash{};
    encoded.resize(std::move(written).expect("the entry fits the buffer of its size"));

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
    const ::fixy::TextView index_line = ::fixy::format(record, "{},{},{},{}\n", events.front().step_id().value,
                                                       events.back().step_id().value, events.size(), hash_digits.view())
                                            .expect("an index line fits 96 characters");

    const EntryPath batch = batch_entry_(hash.raw());
    const EntryPath session_directory = session_entry_(session);
    std::vector<std::uint8_t> stored_batch;

    // The session subdirectory anchors the batch and the index at two
    // levels: root dirfd, then session dirfd, then the leaf under
    // O_NOFOLLOW.  Substituting the intermediate session_events directory
    // needs write access to the Cipher root, which is outside the threat
    // model.  A directory that is there gives eexist.  A batch file of the
    // hash must hold the same bytes, and the replace makes a new batch
    // file durable before the index names it.
    const bool is_indexed = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        static_cast<void>(::fixy::fs::make_directory_at(ctx, root_dirfd_, "session_events").is_ok());
        static_cast<void>(::fixy::fs::make_directory_at(ctx, root_dirfd_, session_directory.c_text()).is_ok());
        return ::fixy::fs::open_dirfd_at(ctx, root_dirfd_, session_directory.c_text())
            .match(
                [&](::fixy::fs::Dirfd session_dir) {
                    const bool is_batch_present =
                        ::fixy::fs::has_entry_at(ctx, session_dir, batch.c_text()).value_or(false);
                    const bool is_batch_stored =
                        is_batch_present
                            ? read_entry_at_(ctx, session_dir, batch.c_text(), encoded.size(), stored_batch)
                                  && stored_batch == encoded
                            : replace_entry_at_(ctx, session_dir, batch.c_text().view(),
                                                std::as_bytes(std::span<const std::uint8_t>{encoded}));
                    return is_batch_stored && append_entry_at_(ctx, session_dir, "index", index_line);
                },
                [](::foundation::os::Errno) { return false; });
    });
    if (!is_indexed) {
        return ContentHash{};
    }
    return hash;
}

std::vector<Cipher::SessionEvent> Cipher::load_session_events_(FileCtx const& file_ctx, OpenView const&,
                                                               ::foundation::effects::Alloc allocation,
                                                               ::fixy::session::SessionTagId session,
                                                               ::fixy::session::StepId from_step) const {
    std::vector<SessionEvent> out;
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
        std::vector<std::uint8_t> bytes;
        const bool is_read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
            return read_entry_at_(ctx, session_dir, batch.c_text(),
                                  cipher::federation::FEDERATION_HEADER_BYTES + MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES,
                                  bytes);
        });
        if (!is_read || bytes.size() < cipher::federation::FEDERATION_HEADER_BYTES) continue;

        auto entry = cipher::federation::deserialize_untrusted_federation_entry(
            std::span<const std::uint8_t>{bytes}, static_cast<std::uint16_t>(::foundation::effects::effect_count));
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
        auto decoded = ::fixy::session::decode_session_log(std::as_bytes(view.payload));
        if (!decoded) continue;
        if (decoded->front().step_id().value != first) continue;
        if (decoded->back().step_id().value != last) continue;
        bool valid_batch = true;
        for (std::size_t i = 0; i < decoded->size(); ++i) {
            const SessionEvent& event = (*decoded)[i];
            if (event.session() != session) {
                valid_batch = false;
                break;
            }
            if (i != 0) {
                const uint64_t prev_step = (*decoded)[i - 1].step_id().value;
                if (prev_step == ::fixy::max_of<uint64_t> || prev_step + 1u != event.step_id().value) {
                    valid_batch = false;
                    break;
                }
            }
            if (have_loaded && event.step_id().value <= highest_loaded) {
                valid_batch = false;
                break;
            }
        }
        if (!valid_batch) continue;

        // The steps of the batch are first, first + 1, ..., last, and
        // last is at or after from_step.  So the events at from_step or
        // after are one suffix of the batch, and it is not empty.  One
        // range insert appends it.
        const std::uint64_t skipped = from_step.value > first ? from_step.value - first : std::uint64_t{0};
        out.insert(out.end(), decoded->begin() + static_cast<std::ptrdiff_t>(skipped), decoded->end());
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

ContentHash Cipher::session_event_batch_hash(std::span<const std::uint8_t> bytes) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL ^ 0x53455353494f4e45ULL ^ bytes.size();
    for (std::uint8_t byte : bytes) {
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
[[gnu::cold]] std::span<const uint8_t> Cipher::cached_bytes(ContentHash hash) const noexcept {
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
        return entry.bytes;
    }
    return {};
}

[[gnu::cold]] void Cipher::remember_cached_bytes(ContentHash hash, std::span<const uint8_t> bytes) const {
    if (!hash || bytes.empty() || bytes.size() > MAX_RESIDENT_CACHE_BYTES) {
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
                                 .bytes = std::vector<uint8_t>(bytes.begin(), bytes.end()),
                             })
                             .is_err();
    if (is_full) [[unlikely]] {
        ::fixy::fatal("the resident cache has no room after its eviction");
    }
}

void Cipher::open_root_(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation, RootPath root,
                        ::fixy::Path<::fixy::tags::source::Sanitized> sanitized_root, ::fixy::fs::Dirfd const& here) {
    // The root opens with O_NOFOLLOW, so a symlinked root fails with
    // ELOOP.  Abort rather than continue: the durable-on-return contract
    // needs a root directory whose identity is stable for this instance's
    // lifetime.
    root_dirfd_ = with_file_ctx_(file_ctx, [&](auto const& ctx) {
                      make_root_directories_(ctx, here, root.c_text().view());
                      return ::fixy::fs::open_dirfd(ctx, std::move(sanitized_root));
                  }).expect("the root of the Cipher does not open as a directory");
    [[maybe_unused]] const bool root_set =
        root_.try_set(::fixy::mint_tagged<::fixy::tags::source::Durable>(std::move(root)));
    [[assume(root_set)]];
    static_cast<void>(with_file_ctx_(
        file_ctx, [&](auto const& ctx) { return ::fixy::fs::make_directory_at(ctx, root_dirfd_, "objects").is_ok(); }));

    load_log(file_ctx, allocation);

    // The log is the record of the commits, so its last commit is the
    // head.  HEAD can lag the log after a failed HEAD write, and it is read
    // only when the log holds no commit.  A missing, long or corrupt HEAD
    // gives no head.
    head_ = latest_committed_head();
    if (head_) {
        return;
    }
    ::fixy::GrowText head_text = ::fixy::mint_grow_text(MAX_HEAD_BYTES);
    const bool is_head_read = with_file_ctx_(file_ctx, [&](auto const& ctx) {
        return ::fixy::fs::read_whole_at(ctx, allocation, root_dirfd_, "HEAD", head_text).is_ok();
    });
    head_ = is_head_read ? parse_head_(head_text.view()).value_or(ContentHash{}) : ContentHash{};
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
