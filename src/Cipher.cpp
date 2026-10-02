// The members of crucible/Cipher.h that read or write files, and the members
// that encode a region or a session-event batch.  The header keeps the
// members that a context parameterizes.

#include <crucible/Cipher.h>

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <crucible/cipher/CipherTierPromotion.h>
#include <crucible/cipher/FederationProtocol.h>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace crucible {

namespace {

// One descriptor that a helper below opened relative to a directory
// descriptor.  open_at_ is the one door: it builds an owner only from
// the value that ::openat returned, and the destructor closes it.
class AnchoredFd {
    int fd_ = -1;

    constexpr explicit AnchoredFd(int fd) noexcept : fd_{fd} {}

    friend AnchoredFd open_at_(int parent_dirfd, const char* relpath, int flags, ::mode_t perms) noexcept;

public:
    AnchoredFd(const AnchoredFd&) = delete("a descriptor is unique; a copy would close it twice");
    AnchoredFd& operator=(const AnchoredFd&) = delete("a descriptor is unique; a copy would close it twice");
    AnchoredFd(AnchoredFd&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}
    AnchoredFd& operator=(AnchoredFd&&) = delete("an owner is built once, by open_at_, and never reassigned");

    ~AnchoredFd() {
        if (fd_ >= 0) {
            ::close(
                fd_);  // SYSCALL-CAP-OK: AnchoredFd releases the descriptor that open_at_ acquired under the CtxFitsCipherPersistence gate of the open view
        }
    }

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int get() const noexcept { return fd_; }
};

[[nodiscard]] AnchoredFd open_at_(int parent_dirfd, const char* relpath, int flags, ::mode_t perms) noexcept {
    return AnchoredFd{::openat(
        parent_dirfd,
        relpath,  // SYSCALL-CAP-OK: Cipher cold-tier anchored open — effects::IO, held by the CtxFitsCipherPersistence gate of the open view that every caller of these helpers holds on the persistence thread
        flags, perms)};
}

// Re-opens `relpath` relative to `parent_dirfd` read-only and
// fdatasyncs it.  Linux permits fdatasync to be interrupted by a
// signal, so EINTR is retried.
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
[[nodiscard]] bool fdatasync_at_(int parent_dirfd, const std::string& relpath) noexcept {
    const AnchoredFd guard = open_at_(parent_dirfd, relpath.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0);
    if (!guard.is_open()) return false;
    int rc;
    do {
        rc = ::fdatasync(
            guard
                .get());  // SYSCALL-CAP-OK: Cipher cold-tier durable flush blocks on disk — effects::IO + effects::Block, held by the CtxFitsCipherPersistence gate of the open view
    } while (rc < 0 && errno == EINTR);
    return rc == 0;
}

// Atomic file-content replace: write `relpath + ".tmp"` relative to
// `parent_dirfd`, fdatasync the bytes, rename onto `relpath`, then fsync
// the parent dirfd.
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
// EINTR is retried on write, fdatasync, renameat and fsync.  If any step
// fails after the tmp file exists, it is unlinked so a later run does not
// trip over a partial-write artifact.
[[nodiscard]] bool atomic_write_at_(int parent_dirfd, const std::string& relpath,
                                    std::span<const std::uint8_t> bytes) noexcept {
    const std::string tmp_relpath = relpath + ".tmp";

    // The nested scope closes the writer before the renameat below,
    // which is what POSIX recommends for atomic-rename writes and keeps
    // no extra descriptor open across the rename window.
    {
        const AnchoredFd write_guard =
            open_at_(parent_dirfd, tmp_relpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (!write_guard.is_open()) return false;

        std::size_t off = 0;
        while (off < bytes.size()) {
            const ssize_t r = ::write(
                write_guard
                    .get(),  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace bytes write — effects::IO, held by the CtxFitsCipherPersistence gate of the open view
                bytes.data() + off, bytes.size() - off);
            if (r < 0) {
                if (errno == EINTR) continue;
                ::unlinkat(parent_dirfd, tmp_relpath.c_str(), 0);
                return false;
            }
            off += static_cast<std::size_t>(r);
        }

        int frc;
        do {
            frc = ::fdatasync(
                write_guard
                    .get());  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace flush blocks on disk — effects::IO + effects::Block, held by the CtxFitsCipherPersistence gate of the open view
        } while (frc < 0 && errno == EINTR);
        if (frc != 0) {
            ::unlinkat(parent_dirfd, tmp_relpath.c_str(), 0);
            return false;
        }
    }

    // Anchoring both sides of the rename at the same dirfd makes the
    // swap immune to a concurrent rename of the parent directory.
    int rrc;
    do {
        rrc = ::renameat(parent_dirfd, tmp_relpath.c_str(), parent_dirfd, relpath.c_str());
    } while (rrc < 0 && errno == EINTR);
    if (rrc != 0) {
        ::unlinkat(parent_dirfd, tmp_relpath.c_str(), 0);
        return false;
    }

    // parent_dirfd is owned by the caller and outlives this call, so
    // the fsync needs no re-open of the parent path.
    int drc;
    do {
        drc = ::fsync(
            parent_dirfd);  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace parent-dir fsync blocks on disk — effects::IO + effects::Block, held by the CtxFitsCipherPersistence gate of the open view
    } while (drc < 0 && errno == EINTR);
    return drc == 0;
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
// write somehow does straddle the block boundary, load_log skips the
// malformed trailing line, the in-memory log still holds the event, and
// HEAD carries the authoritative pointer.
//
// EINTR is retried on write, fdatasync and fsync.  Failure needs no
// cleanup, since the file is append-only by construction.  Returns true
// only when both the record bytes and the directory entry are durable.
[[nodiscard]] bool atomic_append_at_(int parent_dirfd, const std::string& relpath,
                                     std::span<const std::uint8_t> bytes) noexcept {
    {
        const AnchoredFd guard =
            open_at_(parent_dirfd, relpath.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (!guard.is_open()) return false;

        // With O_APPEND and a record smaller than a block, ::write
        // either fails outright or writes every byte.  The loop covers
        // a short write on signal, and each re-issue is a fresh
        // kernel-atomic O_APPEND write.
        std::size_t off = 0;
        while (off < bytes.size()) {
            const ssize_t r = ::write(
                guard
                    .get(),  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append record write — effects::IO, held by the CtxFitsCipherPersistence gate of the open view
                bytes.data() + off, bytes.size() - off);
            if (r < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            off += static_cast<std::size_t>(r);
        }

        int frc;
        do {
            frc = ::fdatasync(
                guard
                    .get());  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append flush blocks on disk — effects::IO + effects::Block, held by the CtxFitsCipherPersistence gate of the open view
        } while (frc < 0 && errno == EINTR);
        if (frc != 0) return false;
    }

    // Makes the file's existence durable for the first record.
    int drc;
    do {
        drc = ::fsync(
            parent_dirfd);  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append parent-dir fsync blocks on disk — effects::IO + effects::Block, held by the CtxFitsCipherPersistence gate of the open view
    } while (drc < 0 && errno == EINTR);
    return drc == 0;
}

// Returns a closed owner on failure.  O_DIRECTORY makes the open fail
// with ENOTDIR if a regular file has been substituted for the
// directory, and O_NOFOLLOW rejects a symlinked leaf.
[[nodiscard]] AnchoredFd open_dir_at_(int parent_dirfd, const char* relpath) noexcept {
    return open_at_(parent_dirfd, relpath, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0);
}

void hex16_(uint64_t value, char (&out)[16]) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 16; ++i) {
        out[15 - i] = kHex[value & 0x0FULL];
        value >>= 4;
    }
}

// Joins the pieces into one string of their total length.  The string
// gets its full size at construction, so the join allocates at most
// one time and never grows.  Complexity: linear in the total length.
[[nodiscard]] std::string join_(std::initializer_list<std::string_view> pieces) {
    std::size_t total = 0;
    for (const std::string_view piece : pieces) {
        total += piece.size();
    }
    std::string joined(total, '\0');
    std::size_t offset = 0;
    for (const std::string_view piece : pieces) {
        if (piece.empty()) continue;  // an empty view can hold a null pointer, which memcpy refuses
        std::memcpy(joined.data() + offset, piece.data(), piece.size());
        offset += piece.size();
    }
    return joined;
}

// Returns "objects/<XX>/<14hex>".  No leading slash: openat resolves a
// relative path under the parent dirfd's inode, not under "/".
[[nodiscard]] auto obj_relpath_(std::uint64_t hash) -> ::fixy::Tagged<std::string, ::fixy::tags::source::CipherPath> {
    char hex[16];
    hex16_(hash, hex);
    std::string path = join_({"objects/", std::string_view{hex, 2}, "/", std::string_view{hex + 2, 14}});
    return ::fixy::mint_tagged<::fixy::tags::source::CipherPath>(std::move(path));
}

[[nodiscard]] std::string session_event_dir_relpath_(::fixy::session::SessionTagId session) {
    char hex[16];
    hex16_(session.value, hex);
    return join_({"session_events/", std::string_view{hex, 16}});
}

[[nodiscard]] bool file_bytes_equal_(const std::string& path, std::span<const std::uint8_t> expected) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto raw = in.tellg();
    if (raw < 0) return false;
    const auto len = static_cast<std::size_t>(raw);
    if (len != expected.size()) return false;
    in.seekg(0, std::ios::beg);

    std::array<std::uint8_t, 4096> actual{};
    std::size_t offset = 0;
    while (offset < expected.size()) {
        const std::size_t remaining = expected.size() - offset;
        const std::size_t n = remaining < actual.size() ? remaining : actual.size();
        in.read(static_cast<char*>(static_cast<void*>(actual.data())), static_cast<std::streamsize>(n));
        if (in.gcount() != static_cast<std::streamsize>(n)) return false;
        if (std::memcmp(actual.data(), expected.data() + offset, n) != 0) {
            return false;
        }
        offset += n;
    }
    return true;
}

// std::from_chars, not std::stoull: stoull throws on malformed input.
// Returns true only when the whole [begin, end) range parsed as a
// number in the given base.
[[nodiscard]] bool parse_u64(const char* begin, const char* end, int base, uint64_t& out) noexcept {
    if (begin >= end) return false;
    auto [p, ec] = std::from_chars(begin, end, out, base);
    return ec == std::errc{} && p == end;
}

[[nodiscard]] bool parse_session_index_line_(std::string_view line, uint64_t& first, uint64_t& last, uint64_t& count,
                                             uint64_t& hash) noexcept {
    const std::size_t p1 = line.find(',');
    if (p1 == std::string_view::npos) return false;
    const std::size_t p2 = line.find(',', p1 + 1);
    if (p2 == std::string_view::npos) return false;
    const std::size_t p3 = line.find(',', p2 + 1);
    if (p3 == std::string_view::npos) return false;
    const char* begin = line.data();
    const char* end = begin + line.size();
    if (!parse_u64(begin, begin + p1, 10, first)) return false;
    if (!parse_u64(begin + p1 + 1, begin + p2, 10, last)) return false;
    if (!parse_u64(begin + p2 + 1, begin + p3, 10, count)) return false;
    if (!parse_u64(begin + p3 + 1, end, 16, hash)) return false;
    return first <= last;
}

// Conservative upper bound on the serialized size of a RegionNode.
//
// The three unconditional terms sum to 352 bytes, so the result is
// positive by construction.  Stating that in the return type means a
// refactor that drops the headroom or makes a header conditional trips
// the mint's precondition here, instead of silently handing the caller
// a zero-length buffer to serialize into.
::fixy::Positive<std::size_t> estimate_serial_size(const RegionNode* region) {
    size_t sz = 64;  // header
    sz += 32;  // region fixed fields
    if (region->plan) {
        sz += 64 + region->plan->num_slots * sizeof(TensorSlot);
    }
    for (uint32_t i = 0; i < region->num_ops; i++) {
        const TraceEntry& te = region->ops[i];
        const size_t n_in = te.num_inputs;
        const size_t n_out = te.num_outputs;
        const size_t n_sca = te.num_scalar_args;
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

ContentHash Cipher::store(OpenView const&, ContentAddressedRegionPayload payload) {
    const RegionNode* region = payload.get();
    if (!region) return ContentHash{};
    const ContentHash hash = region->content_hash;
    if (!hash) return ContentHash{};

    const auto path_tagged = obj_path(hash.raw());
    const std::string& path = path_tagged.value();

    // The skip path also fdatasyncs.  Another process may have written
    // these bytes and died before the kernel flushed them, and this
    // process is about to return the hash as durable.
    const auto obj_rel_tagged = obj_relpath_(hash.raw());
    const std::string& obj_rel = obj_rel_tagged.value();
    if (std::filesystem::exists(path)) {
        if (!fdatasync_at_(root_dirfd_.get(), obj_rel)) {
            return ContentHash{};
        }
        return hash;
    }

    const size_t cap = estimate_serial_size(region).value();
    std::vector<uint8_t> buf(cap);
    const size_t n = serialize_region(SerializedRegion{*region}, SerialBuffer{buf});
    if (n == 0) return ContentHash{};

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream f(path, std::ios::binary);
    if (!f) return ContentHash{};
    f.write(static_cast<const char*>(static_cast<const void*>(buf.data())), static_cast<std::streamsize>(n));
    if (!f) return ContentHash{};

    // Close the stream before the fdatasync barrier so the userspace
    // buffer reaches the kernel.  A crash between the write and the
    // destructor's flush would otherwise lose the bytes silently, and a
    // hash this function returns must be loadable after a crash.
    //
    // The barrier re-opens the file read-only because libstdc++ does not
    // portably expose the ofstream's descriptor.
    //
    // fdatasync rather than fsync is sufficient.  Objects are immutable
    // content-addressed payloads, so the inode never changes after
    // creation, and only the data plus the metadata needed to locate it
    // need durable ordering.
    f.close();
    if (!f) return ContentHash{};
    if (!fdatasync_at_(root_dirfd_.get(), obj_rel)) {
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

Cipher::LoadedContentAddressedRegionPayload Cipher::load_content_addressed(OpenView const&,
                                                                           ::foundation::effects::Alloc a,
                                                                           ContentHash content_hash,
                                                                           Arena& arena) const {
    if (!content_hash) return nullptr;

    const std::span<const uint8_t> cached = cached_bytes(content_hash);
    if (!cached.empty()) {
        const std::optional<LoadedRegionNode> loaded_region = deserialize_region(a, cached, arena);
        if (loaded_region && loaded_region->value()->content_hash == content_hash) {
            return LoadedContentAddressedRegionPayload{loaded_region->value(), true};
        }
    }

    const auto path_tagged = obj_path(content_hash.raw());
    const std::string& path = path_tagged.value();
    if (!std::filesystem::exists(path)) return nullptr;

    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;

    f.seekg(0, std::ios::end);
    const auto raw = f.tellg();
    if (raw < 0) return nullptr;
    const auto len = static_cast<size_t>(raw);
    // The early return and the mint check the same ceiling on purpose.
    // If this function is later split and the early return is dropped,
    // the mint's precondition still fires.
    if (len == 0 || len > MAX_OBJECT_BYTES) return nullptr;
    const ValidatedObjectSize validated_len = ::fixy::mint_refined<::fixy::bounded_above<MAX_OBJECT_BYTES>>(len);
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(validated_len.value());
    f.read(static_cast<char*>(static_cast<void*>(buf.data())), static_cast<std::streamsize>(validated_len.value()));
    if (!f) return nullptr;

    const std::optional<LoadedRegionNode> loaded_region = deserialize_region(a, std::span<const uint8_t>{buf}, arena);
    // The file name is the key, and anyone who writes the store can put
    // any region under any name.  A region whose own hash is not the key
    // is refused, the same as on the cache path above, so a lookup never
    // returns another region.
    if (!loaded_region || loaded_region->value()->content_hash != content_hash) return nullptr;
    remember_cached_bytes(content_hash, std::span<const uint8_t>{buf});
    return LoadedContentAddressedRegionPayload{loaded_region->value(), false};
}

ContentHash Cipher::persist_session_events_(OpenView const&, std::span<const SessionEvent> events) {
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
        if (prev_step == std::numeric_limits<uint64_t>::max() || prev_step + 1 != events[i].step_id().value)
            [[unlikely]] {
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
    const auto written = cipher::federation::serialize_federation_entry(std::span<std::uint8_t>{encoded}, key,
                                                                        std::span<const std::uint8_t>{payload});
    if (!written) return ContentHash{};
    encoded.resize(*written);

    const std::string dir = session_event_dir(session);
    std::filesystem::create_directories(dir);
    const std::string path = session_event_batch_path(session, hash);
    if (std::filesystem::exists(path)) {
        if (!file_bytes_equal_(path, std::span<const std::uint8_t>{encoded.data(), encoded.size()})) {
            return ContentHash{};
        }
    } else {
        std::ofstream out(path, std::ios::binary);
        if (!out) return ContentHash{};
        out.write(static_cast<const char*>(static_cast<const void*>(encoded.data())),
                  static_cast<std::streamsize>(encoded.size()));
        if (!out) return ContentHash{};
    }

    // Record layout: "<first_step>,<last_step>,<count>,<16-hex hash>\n".
    // Longest form is 20 + 1 + 20 + 1 + 20 + 1 + 16 + 1 = 80 bytes, so 96
    // bytes suffices, well under the 4096-byte window in which POSIX
    // makes a single O_APPEND write to a regular file atomic.
    //
    // load_session_events tolerates a malformed trailing line, so a
    // crash mid-record drops one index entry.  The batch file itself is
    // already on disk, and the next persist_session_events call
    // re-appends the entry.
    char idx_rec[96];
    std::size_t off = 0;
    auto [p1, ec1] = std::to_chars(idx_rec + off, idx_rec + sizeof(idx_rec), events.front().step_id().value);
    if (ec1 != std::errc{}) return ContentHash{};
    off = static_cast<std::size_t>(p1 - idx_rec);
    idx_rec[off++] = ',';
    auto [p2, ec2] = std::to_chars(idx_rec + off, idx_rec + sizeof(idx_rec), events.back().step_id().value);
    if (ec2 != std::errc{}) return ContentHash{};
    off = static_cast<std::size_t>(p2 - idx_rec);
    idx_rec[off++] = ',';
    auto [p3, ec3] = std::to_chars(idx_rec + off, idx_rec + sizeof(idx_rec), events.size());
    if (ec3 != std::errc{}) return ContentHash{};
    off = static_cast<std::size_t>(p3 - idx_rec);
    idx_rec[off++] = ',';
    char hex[16];
    hex16_(hash.raw(), hex);
    std::memcpy(idx_rec + off, hex, 16);
    off += 16;
    idx_rec[off++] = '\n';

    // Open the session subdirectory so the index append is anchored at
    // two levels: root dirfd, then session dirfd, then the "index" leaf
    // under O_NOFOLLOW.  Substituting the intermediate session_events
    // directory needs write access to the Cipher root, which is outside
    // the threat model.
    const std::string sess_rel = session_event_dir_relpath_(session);
    const AnchoredFd session_dirfd = open_dir_at_(root_dirfd_.get(), sess_rel.c_str());
    if (!session_dirfd.is_open()) {
        return ContentHash{};
    }

    if (!atomic_append_at_(
            session_dirfd.get(), "index",
            std::span<const std::uint8_t>{static_cast<const std::uint8_t*>(static_cast<const void*>(idx_rec)), off})) {
        return ContentHash{};
    }

    return hash;
}

std::vector<Cipher::SessionEvent> Cipher::load_session_events(OpenView const&, ::fixy::session::SessionTagId session,
                                                              ::fixy::session::StepId from_step) const {
    std::vector<SessionEvent> out;
    std::ifstream index(session_event_dir(session) + "/index");
    if (!index) return out;

    std::string line;
    uint64_t highest_loaded = from_step.value;
    bool have_loaded = false;
    while (std::getline(index, line)) {
        uint64_t first = 0;
        uint64_t last = 0;
        uint64_t count = 0;
        uint64_t raw_hash = 0;
        if (!parse_session_index_line_(line, first, last, count, raw_hash)) {
            continue;
        }
        if (last < from_step.value) continue;
        if (count == 0 || count > MAX_SESSION_EVENT_BATCH_EVENTS) continue;
        if ((last - first + 1u) != count) continue;

        const ContentHash hash{raw_hash};
        const std::string path = session_event_batch_path(session, hash);
        std::ifstream batch(path, std::ios::binary);
        if (!batch) continue;

        batch.seekg(0, std::ios::end);
        const auto raw_len = batch.tellg();
        if (raw_len < 0) continue;
        const auto len = static_cast<std::size_t>(raw_len);
        if (len < cipher::federation::FEDERATION_HEADER_BYTES
            || len > cipher::federation::FEDERATION_HEADER_BYTES + MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
            continue;
        }
        batch.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> bytes(len);
        batch.read(static_cast<char*>(static_cast<void*>(bytes.data())), static_cast<std::streamsize>(bytes.size()));
        if (!batch) continue;

        auto view = cipher::federation::deserialize_untrusted_federation_entry(
            std::span<const std::uint8_t>{bytes}, static_cast<std::uint16_t>(::foundation::effects::effect_count));
        if (!view) continue;
        if (view->header.content_hash != hash) continue;
        if (view->header.row_hash != SESSION_EVENT_FEDERATION_ROW_HASH) {
            continue;
        }
        if (session_event_batch_hash(view->payload) != hash) continue;
        if ((view->payload.size() % sizeof(SessionEvent)) != 0u) continue;

        const std::size_t n = view->payload.size() / sizeof(SessionEvent);
        if (n != count) continue;

        // The hashes prove only that the batch is the one its writer
        // stored, and anyone who writes the store can compute them.  So
        // each record goes through the event decoder, which is the one
        // route from bytes to an event and refuses a record with a byte
        // outside its field.
        auto decoded = ::fixy::session::decode_session_log(std::as_bytes(view->payload));
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
                if (prev_step == std::numeric_limits<uint64_t>::max() || prev_step + 1u != event.step_id().value) {
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
    if (h == std::numeric_limits<uint64_t>::max()) --h;
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

void Cipher::advance_head(OpenView const&, ContentHash content_hash, uint64_t step_id,
                          ::fixy::MonotonicClockBytes<std::uint64_t> committed_at) {
    // Weakly increasing, not strictly: duplicate step ids are accepted
    // at this gate.  The strict consecutive-by-one check belongs to the
    // session-event batch invariant, not to the head log.
    if (!log_.empty()) {
        uint64_t const ordering[2] = {log_.back().step_id().value, step_id};
        CRUCIBLE_PRE(
            ::foundation::decide::weakly_increasing<std::uint64_t>(std::span<const std::uint64_t>{ordering, 2}));
    }
    head_ = content_hash;

    // Ignoring the helper's result is deliberate.  A failed HEAD write
    // leaves the cached head_ set and the log append below as the
    // recovery anchor: the next open() recovers the committed pointer
    // by scanning the log even when HEAD is stale or missing.
    {
        char hex[16];
        hex16_(content_hash.raw(), hex);
        std::uint8_t buf[17];
        std::memcpy(buf, hex, 16);
        buf[16] = static_cast<std::uint8_t>('\n');
        (void)atomic_write_at_(root_dirfd_.get(), "HEAD", std::span<const std::uint8_t>{buf, sizeof(buf)});
    }

    const std::uint64_t committed_at_ns = committed_at.peek();
    log_.append(SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted, ::fixy::session::StepId{step_id},
                                           ::fixy::session::StateHash{content_hash.raw()}, committed_at_ns));
    {
        // Record layout: "<step_id>,<16-hex content_hash>,<ts>\n".
        // Longest form is 20 decimal digits + 1 + 16 + 1 + 20 decimal
        // digits + 1 = 59 bytes, so 64 bytes suffices.  That is well
        // under the 4096-byte window in which POSIX makes a single
        // O_APPEND write to a regular file atomic.
        char rec[64];
        std::size_t off = 0;
        auto [p1, ec1] = std::to_chars(rec + off, rec + sizeof(rec), step_id);
        if (ec1 != std::errc{}) std::abort();
        off = static_cast<std::size_t>(p1 - rec);
        rec[off++] = ',';
        char hex[16];
        hex16_(content_hash.raw(), hex);
        std::memcpy(rec + off, hex, 16);
        off += 16;
        rec[off++] = ',';
        auto [p2, ec2] = std::to_chars(rec + off, rec + sizeof(rec), committed_at_ns);
        if (ec2 != std::errc{}) std::abort();
        off = static_cast<std::size_t>(p2 - rec);
        rec[off++] = '\n';
        (void)atomic_append_at_(
            root_dirfd_.get(), "log",
            std::span<const std::uint8_t>{static_cast<const std::uint8_t*>(static_cast<const void*>(rec)), off});
    }

    CRUCIBLE_POST(0, head_ == content_hash);
    CRUCIBLE_POST(0, !log_.empty());
    CRUCIBLE_POST(0, log_.back().step_id().value == step_id);
}

// HEAD holds the hash in hexadecimal, with a newline after it.
// std::from_chars is exception-free, unlike std::stoull which throws on
// malformed input, so a corrupt HEAD gives no hash and the caller falls
// through to the log.
std::optional<ContentHash> Cipher::parse_head_(std::span<const char> bytes) noexcept {
    uint64_t raw = 0;
    const char* begin = bytes.data();
    const char* end = bytes.data() + bytes.size();
    auto [p, ec] = std::from_chars(begin, end, raw, /*base=*/16);
    if (ec == std::errc{} && p != begin) {
        return ContentHash{raw};
    }
    return std::nullopt;
}

auto Cipher::obj_path(uint64_t hash) const -> ::fixy::Tagged<std::string, ::fixy::tags::source::CipherPath> {
    char hex[16];
    hex16_(hash, hex);
    const std::string& root = root_str();
    if (root.size() > MAX_ROOT_PATH_BYTES) {
        std::abort();
    }
    std::string path = join_({root, "/objects/", std::string_view{hex, 2}, "/", std::string_view{hex + 2, 14}});
    return ::fixy::mint_tagged<::fixy::tags::source::CipherPath>(std::move(path));
}

std::string Cipher::session_event_dir(::fixy::session::SessionTagId session) const {
    char hex[16];
    hex16_(session.value, hex);
    return join_({root_str(), "/session_events/", std::string_view{hex, 16}});
}

std::string Cipher::session_event_batch_path(::fixy::session::SessionTagId session, ContentHash hash) const {
    char session_hex[16];
    hex16_(session.value, session_hex);
    char hash_hex[16];
    hex16_(hash.raw(), hash_hex);
    return join_({root_str(), "/session_events/", std::string_view{session_hex, 16}, "/",
                  std::string_view{hash_hex, 16}, ".cfed"});
}

std::span<const uint8_t> Cipher::cached_bytes(ContentHash hash) const noexcept {
    for (std::size_t i = 0; i < resident_cache_.size(); ++i) {
        const CachedObjectBytes& entry = resident_cache_[i];
        if (entry.hash == hash) {
            if (i + 1 != resident_cache_.size()) {
                CachedObjectBytes hit = std::move(resident_cache_[i]);
                resident_cache_.erase(resident_cache_.begin() + static_cast<std::ptrdiff_t>(i));
                resident_cache_.push_back(std::move(hit));
            }
            return std::span<const uint8_t>{resident_cache_.back().bytes};
        }
    }
    return {};
}

void Cipher::remember_cached_bytes(ContentHash hash, std::span<const uint8_t> bytes) const {
    if (!hash || bytes.empty() || bytes.size() > MAX_RESIDENT_CACHE_BYTES) {
        return;
    }
    for (const CachedObjectBytes& entry : resident_cache_) {
        if (entry.hash == hash) return;
    }

    // The loop leaves at most MAX_RESIDENT_CACHE_ENTRIES - 1 entries, so
    // the push below stays in the capacity of the cache.
    while (!resident_cache_.empty()
           && (resident_cache_.size() >= MAX_RESIDENT_CACHE_ENTRIES
               || resident_cache_bytes_ + bytes.size() > MAX_RESIDENT_CACHE_BYTES)) {
        resident_cache_bytes_ -= resident_cache_.front().bytes.size();
        resident_cache_.erase(resident_cache_.begin());
    }

    resident_cache_bytes_ += bytes.size();
    resident_cache_.push_back(CachedObjectBytes{
        .hash = hash,
        .bytes = std::vector<uint8_t>(bytes.begin(), bytes.end()),
    });
}

void Cipher::load_log() {
    std::ifstream f(root_str() + "/log");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const size_t p1 = line.find(',');
        if (p1 == std::string::npos) continue;
        const size_t p2 = line.find(',', p1 + 1);
        if (p2 == std::string::npos) continue;

        const char* begin = line.data();
        uint64_t step_id = 0;
        uint64_t raw_hash = 0;
        uint64_t ts_ns = 0;
        if (!parse_u64(begin, begin + p1, 10, step_id)) continue;
        if (!parse_u64(begin + p1 + 1, begin + p2, 16, raw_hash)) continue;
        if (!parse_u64(begin + p2 + 1, begin + line.size(), 10, ts_ns)) continue;
        // The log only grows in step order, and its append checks that
        // order with a contract that aborts.  A line out of order is a
        // corrupt line, so it is skipped like the others.
        if (!log_.empty() && step_id < log_.back().step_id().value) continue;

        log_.append(SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted,
                                               ::fixy::session::StepId{step_id}, ::fixy::session::StateHash{raw_hash},
                                               ts_ns));
    }
}

}  // namespace crucible
