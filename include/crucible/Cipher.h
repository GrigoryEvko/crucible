#pragma once

// Content-addressed object store for Merkle DAG nodes.
//
// On-disk layout:
//   $root/objects/<first2hex>/<remaining14hex>  — one file per node
//   $root/HEAD                                  — hex string: current active hash + "\n"
//   $root/log                                   — append-only: "step_id,hash_hex,ts_ns\n"
//
// A Cipher is not thread-safe.  One thread owns it.

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/Serialize.h>
#include <crucible/cipher/CipherTierPromotion.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/fixy/Diag.h>
#include <crucible/fixy/Handle.h>
#include <crucible/fixy/Is.h>
#include <crucible/fixy/SessContentAddr.h>
#include <crucible/fixy/SessEventLog.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Time.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/source/_Path.h>
#include <crucible/safety/ClockSource.h>
// safety/Decide.h, safety/Post.h and safety/Pre.h are included directly
// rather than through the fixy umbrella.  The umbrella header that
// re-exports the contract macros includes this header, so reaching the
// macros through it would be a circular include.
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Post.h>
#include <crucible/safety/_Pre.h>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible {

namespace cipher {

template <typename T>
class [[nodiscard]] ContentAddressedPayload {
public:
    using value_type = T;
    using payload_type = crucible::fixy::sess::contentaddr::ContentAddressed<T>;

    constexpr explicit ContentAddressedPayload(const T* value) noexcept : value_(value) {}

    [[nodiscard]] constexpr const T* get() const noexcept { return value_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != nullptr; }

private:
    const T* value_ = nullptr;
};

template <typename T>
class [[nodiscard]] LoadedContentAddressedPayload {
public:
    using value_type = T;
    using payload_type = crucible::fixy::sess::contentaddr::ContentAddressed<T>;

    constexpr LoadedContentAddressedPayload() noexcept = default;
    constexpr LoadedContentAddressedPayload(std::nullptr_t) noexcept {}

    constexpr LoadedContentAddressedPayload(T* value, bool cache_hit) noexcept : value_(value), cache_hit_(cache_hit) {}

    [[nodiscard]] constexpr T* get() const noexcept { return value_; }
    [[nodiscard]] constexpr bool cache_hit() const noexcept { return cache_hit_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != nullptr; }
    [[nodiscard]] constexpr operator T*() const noexcept { return value_; }

private:
    T* value_ = nullptr;
    bool cache_hit_ = false;
};

template <typename T>
[[nodiscard]] constexpr ContentAddressedPayload<T> content_addressed_payload(const T* value) noexcept {
    return ContentAddressedPayload<T>{value};
}

}  // namespace cipher

class CRUCIBLE_OWNER Cipher {
public:
    static constexpr std::size_t MAX_ROOT_PATH_BYTES = 4096;
    static constexpr std::size_t OBJECT_PATH_SUFFIX_BYTES = sizeof("/objects/") - 1 + 2 + 1 + 14;

    using ContentAddressedRegionPayload = cipher::ContentAddressedPayload<RegionNode>;
    using LoadedContentAddressedRegionPayload = cipher::LoadedContentAddressedPayload<RegionNode>;
    using SessionEvent = crucible::fixy::sess::eventlog::SessionEvent;

    using persist_session_events_required_row = ::crucible::CipherSessionEventPersistenceRow;

    inline static constexpr RowHash SESSION_EVENT_FEDERATION_ROW_HASH{
        ::crucible::fixy::diag::row_hash_contribution_v<persist_session_events_required_row>};

    static_assert(static_cast<bool>(SESSION_EVENT_FEDERATION_ROW_HASH));
    static_assert(sizeof(SessionEvent) == 72, "Cipher session-event persistence is pinned to the SessionEvent "
                                              "cold-tier wire size.");
    static_assert(std::is_trivially_copyable_v<SessionEvent>,
                  "Cipher session-event persistence bulk-serializes SessionEvent "
                  "bytes and therefore requires a trivially-copyable payload.");

    [[nodiscard]] static constexpr ContentAddressedRegionPayload content_addressed(const RegionNode* region) noexcept {
        return ContentAddressedRegionPayload{region};
    }

    // The path check here is string-level only.  Symlink defense lives in
    // the O_NOFOLLOW-anchored openat helpers below.
    [[gnu::cold]] static Cipher open(crucible::fixy::wrap::Path<crucible::fixy::tags::source::External> root_external) {
        Cipher c;

        auto sanitized_e = crucible::fixy::wrap::sanitize_path(std::move(root_external));
        if (!sanitized_e) {
            return c;
        }

        const std::string root = sanitized_e->value().string();

        // The sanitizer enforces a wider path cap than this one.  The
        // narrower cap here is the bound the rest of the buffer math in
        // this file depends on, so both checks stay.
        if (root.empty() || root.size() > MAX_ROOT_PATH_BYTES) {
            return c;
        }
        [[maybe_unused]] const bool root_set =
            c.root_.try_set(crucible::fixy::wrap::Tagged<std::string, crucible::fixy::tags::source::Durable>{root});
        [[assume(root_set)]];
        std::filesystem::create_directories(root + "/objects");

        // O_NOFOLLOW makes a symlinked root fail with ELOOP.  Abort rather
        // than continue: the durable-on-return contract needs a root
        // directory whose identity is stable for this instance's lifetime.
        {
            const int dfd = ::open(root.c_str(), O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            if (dfd < 0) std::abort();
            c.root_dirfd_ = crucible::fixy::handle::FileHandle{dfd};
        }

        c.load_log();

        // HEAD is the authoritative pointer and overrides the log's last
        // hash.  std::from_chars is exception-free, unlike std::stoull which
        // throws on malformed input and so cannot be used under
        // -fno-exceptions, so a corrupt HEAD falls through to the log.
        const std::string head_path = root + "/HEAD";
        auto hf_e = crucible::fixy::handle::open_read(head_path.c_str());
        bool head_from_file = false;
        if (hf_e && hf_e->is_open()) {
            auto& hf = *hf_e;
            char buf[32];
            const ssize_t n = ::read(hf.get(), buf, sizeof(buf));
            if (n > 0) {
                uint64_t raw = 0;
                const auto* begin = buf;
                const auto* end = buf + n;
                auto [p, ec] = std::from_chars(begin, end, raw, /*base=*/16);
                if (ec == std::errc{} && p != begin) {
                    c.head_ = ContentHash{raw};
                    head_from_file = true;
                }
            }
        }
        if (!head_from_file) {
            c.head_ = c.latest_committed_head();
        }

        return c;
    }

    Cipher() = default;
    Cipher(const Cipher&) = delete("Cipher holds mutable log state; move instead");
    Cipher& operator=(const Cipher&) = delete("Cipher holds mutable log state; move instead");
    Cipher(Cipher&&) = default;
    Cipher& operator=(Cipher&&) = default;

    [[nodiscard]] bool is_open() const noexcept { return root_.has_value(); }

    using OpenView = ::crucible::CipherOpenView;

    [[nodiscard]] OpenView mint_open_view() const noexcept {
        // CRUCIBLE_PRE rather than a pre() clause: on this toolchain a pre()
        // predicate that reads a member through `this` is silently skipped
        // at consteval.  Every in-body precondition in this file has the
        // same reason.
        CRUCIBLE_PRE(is_open());
        return crucible::fixy::wrap::mint_view<cipher_state::Open>(*this);
    }

    [[nodiscard]] friend constexpr bool view_ok(Cipher const& c, std::type_identity<cipher_state::Open>) noexcept {
        return c.is_open();
    }

    [[nodiscard]] ContentHash store(OpenView const&, ContentAddressedRegionPayload payload, const MetaLog* meta_log) {
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
        const size_t n = serialize_region(region, meta_log, std::span<uint8_t>{buf});
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

    [[nodiscard]] crucible::fixy::wrap::Wait<crucible::fixy::wrap::WaitStrategy_v::Block, ContentHash>
    store_pinned(OpenView const& view, ContentAddressedRegionPayload payload, const MetaLog* meta_log) {
        return crucible::fixy::wrap::Wait<crucible::fixy::wrap::WaitStrategy_v::Block, ContentHash>{
            store(view, payload, meta_log)};
    }

    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Warm<ContentHash>
    publish_warm(OpenView const& view, ContentAddressedRegionPayload payload, const MetaLog* meta_log) {
        return crucible::fixy::wrap::cipher_tier::Warm<ContentHash>{store(view, payload, meta_log)};
    }

    // The type declares Hot residency, but nothing is replicated.  The
    // returned none hash lets a caller testing the hash tell that no
    // Hot-tier store happened.
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Hot<ContentHash>
    publish_hot(OpenView const&, ContentAddressedRegionPayload /*payload*/, const MetaLog* /*meta_log*/) noexcept {
        return cipher::mint_promote<crucible::fixy::wrap::CipherTierTag_v::Cold,
                                    crucible::fixy::wrap::CipherTierTag_v::Hot>(
            crucible::fixy::wrap::cipher_tier::Cold<ContentHash>{ContentHash{}});
    }

    // The type declares Cold residency, but nothing is written to durable
    // storage.  The returned none hash lets a caller testing the hash tell
    // that no Cold-tier store happened.
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Cold<ContentHash>
    publish_cold(OpenView const&, ContentAddressedRegionPayload /*payload*/, const MetaLog* /*meta_log*/) noexcept {
        return cipher::mint_demote<crucible::fixy::wrap::CipherTierTag_v::Hot,
                                   crucible::fixy::wrap::CipherTierTag_v::Cold>(
            crucible::fixy::wrap::cipher_tier::Hot<ContentHash>{ContentHash{}});
    }

    // The commit_per_* family pairs a declared data lifetime with the
    // storage tier that matches it.  The rejection direction is the point:
    // a request-scoped value does not satisfy the fleet-scoped requirement,
    // so commit_per_fleet refuses it and request-scoped state cannot leak
    // into durable fleet-wide storage.

    template <typename W>
        requires(crucible::fixy::is::is_opaque_lifetime_v<W>
                 && W::template satisfies<crucible::fixy::wrap::Lifetime_v::PER_REQUEST>)
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Hot<ContentHash>
    commit_per_request(OpenView const& view, W lifetime_pinned_region, const MetaLog* meta_log) noexcept {
        const RegionNode* region = std::move(lifetime_pinned_region).consume();
        return publish_hot(view, content_addressed(region), meta_log);
    }

    template <typename W>
        requires(crucible::fixy::is::is_opaque_lifetime_v<W>
                 && W::template satisfies<crucible::fixy::wrap::Lifetime_v::PER_PROGRAM>)
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Warm<ContentHash>
    commit_per_program(OpenView const& view, W lifetime_pinned_region, const MetaLog* meta_log) {
        const RegionNode* region = std::move(lifetime_pinned_region).consume();
        return publish_warm(view, content_addressed(region), meta_log);
    }

    template <typename W>
        requires(crucible::fixy::is::is_opaque_lifetime_v<W>
                 && W::template satisfies<crucible::fixy::wrap::Lifetime_v::PER_FLEET>)
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Cold<ContentHash>
    commit_per_fleet(OpenView const& view, W lifetime_pinned_region, const MetaLog* meta_log) noexcept {
        const RegionNode* region = std::move(lifetime_pinned_region).consume();
        return publish_cold(view, content_addressed(region), meta_log);
    }

    // Real regions sit at a megabyte or less.  The ceiling leaves room for
    // mixture-of-experts and long-horizon traces while rejecting a corrupt
    // or adversarial length that would make the loader allocate SIZE_MAX.
    static constexpr size_t MAX_OBJECT_BYTES = size_t{256} << 20;

    using ValidatedObjectSize =
        crucible::fixy::wrap::Refined<crucible::fixy::wrap::bounded_above<MAX_OBJECT_BYTES>, size_t>;

    // const even though it writes the resident cache: the cache is
    // process-local acceleration for the content-addressed quotient, not
    // durable Cipher state.  A hit materializes from bytes already seen
    // under this hash and touches no filesystem.
    [[nodiscard]] LoadedContentAddressedRegionPayload
    load_content_addressed(OpenView const&, effects::Alloc a, ContentHash content_hash, Arena& arena) const {
        if (!content_hash) return nullptr;

        const std::span<const uint8_t> cached = cached_bytes(content_hash);
        if (!cached.empty()) {
            const LoadedRegionNode loaded_region = deserialize_region(a, cached, arena);
            RegionNode* region = loaded_region.value();
            if (region && region->content_hash == content_hash) {
                return LoadedContentAddressedRegionPayload{region, true};
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
        // The early return and the Refined constructor check the same
        // ceiling on purpose.  If this function is later split and the
        // early return is dropped, the constructor's contract still fires.
        if (len == 0 || len > MAX_OBJECT_BYTES) return nullptr;
        const ValidatedObjectSize validated_len{len};
        f.seekg(0, std::ios::beg);

        std::vector<uint8_t> buf(validated_len.value());
        f.read(static_cast<char*>(static_cast<void*>(buf.data())), static_cast<std::streamsize>(validated_len.value()));
        if (!f) return nullptr;

        const LoadedRegionNode loaded_region = deserialize_region(a, std::span<const uint8_t>{buf}, arena);
        RegionNode* region = loaded_region.value();
        if (!region) return nullptr;
        remember_cached_bytes(content_hash, std::span<const uint8_t>{buf});
        return LoadedContentAddressedRegionPayload{region, false};
    }

    // content_hash must be non-zero.  Zero is the sentinel for "no content",
    // so a step recorded with hash zero is indistinguishable from "before
    // the first commit" and corrupts the binary search in hash_at_step.
    void advance_head(OpenView const&, ContentHash content_hash, uint64_t step_id)
        pre(::crucible::decide::is_non_zero(content_hash)) {
        // Weakly increasing, not strictly: duplicate step ids are accepted
        // at this gate.  The strict consecutive-by-one check belongs to the
        // session-event batch invariant, not to the head log.
        if (!log_.empty()) {
            uint64_t const ordering[2] = {log_.back().step_id.value, step_id};
            CRUCIBLE_PRE(
                ::crucible::decide::weakly_increasing<std::uint64_t>(std::span<const std::uint64_t>{ordering, 2}));
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

        auto ts_bytes = now_ns();
        const uint64_t ts = std::move(ts_bytes).consume();
        log_.emplace(
            LogEntry::cipher_store_committed(crucible::fixy::sess::eventlog::StepId{step_id}, content_hash, ts));
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
            auto [p2, ec2] = std::to_chars(rec + off, rec + sizeof(rec), ts);
            if (ec2 != std::errc{}) std::abort();
            off = static_cast<std::size_t>(p2 - rec);
            rec[off++] = '\n';
            (void)atomic_append_at_(
                root_dirfd_.get(), "log",
                std::span<const std::uint8_t>{static_cast<const std::uint8_t*>(static_cast<const void*>(rec)), off});
        }

        CRUCIBLE_POST(0, head_ == content_hash);
        CRUCIBLE_POST(0, !log_.empty());
        CRUCIBLE_POST(0, log_.back().step_id.value == step_id);
    }

    // The row constraint is what keeps a foreground caller out.
    // record_event writes HEAD and appends to the log, so it needs IO and
    // Block.  A hot-path context holds neither, and performing file I/O
    // there would break replay determinism.
    using record_event_required_row =
        ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;

    // The asserts below pin the exact row contents.  Narrowing the alias to
    // Row<IO> would still compile and still reject an empty row, so it would
    // silently drop the Block half of the fence without these.
    static_assert(
        std::is_same_v<record_event_required_row,
                       ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>>,
        "Cipher::record_event_required_row must be exactly Row<IO, Block>.  "
        "Adding or removing an atom changes the fence every call site "
        "depends on.");
    static_assert(::crucible::effects::row_size_v<record_event_required_row> == 2u,
                  "record_event_required_row must be exactly 2 atoms (IO + Block).");
    static_assert(
        ::crucible::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete-row static_assert (record_event_required_row), not a Ctx capability check
                record_event_required_row, ::crucible::effects::Effect::IO>,
        "record_event_required_row must contain Effect::IO.  It is the "
        "fence's reason for existence: HEAD and log file writes.");
    static_assert(
        ::crucible::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete-row static_assert (record_event_required_row), not a Ctx capability check
                record_event_required_row, ::crucible::effects::Effect::Block>,
        "record_event_required_row must contain Effect::Block.  File writes "
        "block on the kernel.");
    static_assert(std::is_same_v<persist_session_events_required_row, record_event_required_row>,
                  "Session-event persistence uses the same IO+Block row fence "
                  "as Cipher::record_event.");

    template <typename CallerRow>
        requires ::crucible::effects::Subrow<record_event_required_row, CallerRow>
    void record_event(OpenView const& view, ContentHash content_hash, uint64_t step_id) {
        advance_head(view, content_hash, step_id);
    }

    template <typename CallerRow>
        requires ::crucible::effects::Subrow<persist_session_events_required_row, CallerRow>
    [[nodiscard]] ContentHash persist_session_events(OpenView const&, std::span<const SessionEvent> events) {
        if (events.empty()) return ContentHash{};

        if (events.size() > MAX_SESSION_EVENT_BATCH_EVENTS) {
            return ContentHash{};
        }

        const crucible::fixy::sess::eventlog::SessionTagId session = events.front().session;
        for (std::size_t i = 1; i < events.size(); ++i) {
            if (events[i].session != session) [[unlikely]] {
                return ContentHash{};
            }
            const uint64_t prev_step = events[i - 1].step_id.value;
            if (prev_step == std::numeric_limits<uint64_t>::max() || prev_step + 1 != events[i].step_id.value)
                [[unlikely]] {
                return ContentHash{};
            }
        }

        const std::size_t payload_bytes = events.size() * sizeof(SessionEvent);
        if (payload_bytes > MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
            return ContentHash{};
        }

        // SessionEvent is trivially copyable and standard layout, so a byte
        // view of it is permitted.  No uint8_t array lifetime is started:
        // the span is read-only and the hash consumes the bytes structurally.
        const auto payload = std::span<const std::uint8_t>{
            static_cast<const std::uint8_t*>(static_cast<const void*>(events.data())), payload_bytes};
        const ContentHash hash = session_event_payload_hash_(payload);
        const KernelCacheKey key{hash, SESSION_EVENT_FEDERATION_ROW_HASH};

        std::vector<std::uint8_t> encoded(cipher::federation::FEDERATION_HEADER_BYTES + payload.size());
        const auto written =
            cipher::federation::serialize_federation_entry(std::span<std::uint8_t>{encoded}, key, payload);
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
        auto [p1, ec1] = std::to_chars(idx_rec + off, idx_rec + sizeof(idx_rec), events.front().step_id.value);
        if (ec1 != std::errc{}) return ContentHash{};
        off = static_cast<std::size_t>(p1 - idx_rec);
        idx_rec[off++] = ',';
        auto [p2, ec2] = std::to_chars(idx_rec + off, idx_rec + sizeof(idx_rec), events.back().step_id.value);
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
        crucible::fixy::handle::FileHandle session_dirfd = open_dir_at_(root_dirfd_.get(), sess_rel.c_str());
        if (!session_dirfd.is_open()) {
            return ContentHash{};
        }

        if (!atomic_append_at_(session_dirfd.get(), "index",
                               std::span<const std::uint8_t>{
                                   static_cast<const std::uint8_t*>(static_cast<const void*>(idx_rec)), off})) {
            return ContentHash{};
        }

        return hash;
    }

    [[nodiscard]] std::vector<SessionEvent>
    load_session_events(OpenView const&, crucible::fixy::sess::eventlog::SessionTagId session,
                        crucible::fixy::sess::eventlog::StepId from_step = {}) const {
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
            batch.read(static_cast<char*>(static_cast<void*>(bytes.data())),
                       static_cast<std::streamsize>(bytes.size()));
            if (!batch) continue;

            auto view = cipher::federation::deserialize_untrusted_federation_entry(
                std::span<const std::uint8_t>{bytes},
                static_cast<std::uint16_t>(::crucible::effects::OsUniverse::cardinality));
            if (!view) continue;
            if (view->header.content_hash != hash) continue;
            if (view->header.row_hash != SESSION_EVENT_FEDERATION_ROW_HASH) {
                continue;
            }
            if (session_event_payload_hash_(view->payload) != hash) continue;
            if ((view->payload.size() % sizeof(SessionEvent)) != 0u) continue;

            const std::size_t n = view->payload.size() / sizeof(SessionEvent);
            if (n != count) continue;

            std::vector<SessionEvent> decoded(n);
            std::memcpy(decoded.data(), view->payload.data(), view->payload.size());
            if (decoded.front().step_id.value != first) continue;
            if (decoded.back().step_id.value != last) continue;
            bool valid_batch = true;
            for (std::size_t i = 0; i < decoded.size(); ++i) {
                if (decoded[i].session != session) {
                    valid_batch = false;
                    break;
                }
                if (i != 0) {
                    const uint64_t prev_step = decoded[i - 1].step_id.value;
                    if (prev_step == std::numeric_limits<uint64_t>::max()
                        || prev_step + 1u != decoded[i].step_id.value) {
                        valid_batch = false;
                        break;
                    }
                }
                if (have_loaded && decoded[i].step_id.value <= highest_loaded) {
                    valid_batch = false;
                    break;
                }
            }
            if (!valid_batch) continue;

            out.reserve(out.size() + decoded.size());
            for (const SessionEvent& event : decoded) {
                if (event.step_id.value >= from_step.value) {
                    out.push_back(event);
                    highest_loaded = event.step_id.value;
                    have_loaded = true;
                }
            }
        }
        return out;
    }

    [[nodiscard]] ContentHash hash_at_step(OpenView const&, uint64_t step_id) const {
        if (log_.empty()) return ContentHash{};

        // Find the first entry after step_id, then walk back to the latest
        // event that actually commits the head.  The log entry type is the
        // shared session-event record, which can also carry events that do
        // not move the head.
        std::size_t lo = 0;
        std::size_t hi = log_.size();

        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (log_[mid].step_id.value <= step_id) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        while (lo > 0) {
            --lo;
            const LogEntry& entry = log_[lo];
            if (entry.commits_cipher_head()) {
                return entry.cipher_content_hash();
            }
        }
        return ContentHash{};
    }

    [[nodiscard]] ContentHash head() const { return head_; }
    [[nodiscard]] bool empty() const { return !head_; }
    [[nodiscard]] const std::string& root() const CRUCIBLE_LIFETIMEBOUND { return root_str(); }

private:
    using LogEntry = crucible::fixy::sess::eventlog::SessionEvent;

    static_assert(std::is_same_v<LogEntry, crucible::fixy::sess::eventlog::SessionEvent>);
    static_assert(sizeof(LogEntry) == 72);
    static_assert(std::is_trivially_copyable_v<LogEntry>);

    struct CachedObjectBytes {
        ContentHash hash;
        std::vector<uint8_t> bytes;
    };

    static constexpr size_t MAX_RESIDENT_CACHE_BYTES = size_t{8} << 20;
    static constexpr size_t MAX_RESIDENT_CACHE_ENTRIES = 64;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES = size_t{64} << 20;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_EVENTS =
        MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES / sizeof(SessionEvent);

    using LogEntryByStepId = crucible::fixy::sess::eventlog::StepIdKeyFn;
    using LogEntryStepLess = crucible::fixy::sess::eventlog::StepIdLess;

    crucible::fixy::wrap::WriteOnce<crucible::fixy::wrap::Tagged<std::string, crucible::fixy::tags::source::Durable>>
        root_;
    // Dirfd anchor for the Cipher root.  Every helper that touches the
    // on-disk tree opens relative to this fd with O_NOFOLLOW, which buys
    // three properties:
    //
    //   (1) The root itself cannot be a symlink.  The dirfd open uses
    //       O_NOFOLLOW and fails with ELOOP.
    //
    //   (2) The root survives a rename underneath us.  The dirfd keeps
    //       referring to the original inode even if the path moves.
    //
    //   (3) The leaf component of any helper-relative open cannot be a
    //       symlink, so a substituted HEAD, log, index or object file
    //       fails with ELOOP instead of redirecting the write.
    //
    // O_NOFOLLOW inspects only the trailing pathname component, never the
    // intermediate directories.  Substituting an intermediate directory
    // needs write access to the Cipher root, which is outside the threat
    // model: whoever owns this dirfd also owns the root's permissions.
    crucible::fixy::handle::FileHandle root_dirfd_{};
    ContentHash head_{};

    [[nodiscard]] const std::string& root_str() const noexcept {
        [[assume(root_.has_value())]];
        return root_.get_assuming_set().value();
    }
    crucible::fixy::wrap::OrderedAppendOnly<LogEntry, LogEntryByStepId, LogEntryStepLess> log_;
    mutable std::vector<CachedObjectBytes> resident_cache_;
    mutable size_t resident_cache_bytes_ = 0;

    [[nodiscard]] ContentHash latest_committed_head() const noexcept {
        std::size_t i = log_.size();
        while (i > 0) {
            --i;
            const LogEntry& entry = log_[i];
            if (entry.commits_cipher_head()) {
                return entry.cipher_content_hash();
            }
        }
        return ContentHash{};
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
    [[nodiscard]] static bool fdatasync_at_(int parent_dirfd, const std::string& relpath) noexcept {
        const int fd = ::openat(
            parent_dirfd,
            relpath
                .c_str(),  // SYSCALL-CAP-OK: Cipher cold-tier durable-flush open — effects::IO, held by the record_event/persist_session_events Subrow<Row<IO,Block>> boundary that reaches this static helper on the Bg persistence thread
            O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) return false;
        crucible::fixy::handle::FileHandle guard{fd};
        int rc;
        do {
            rc = ::fdatasync(
                fd);  // SYSCALL-CAP-OK: Cipher cold-tier durable flush blocks on disk — effects::IO + effects::Block, held by the Subrow<Row<IO,Block>> persistence boundary
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
    [[nodiscard]] static bool atomic_write_at_(int parent_dirfd, const std::string& relpath,
                                               std::span<const std::uint8_t> bytes) noexcept {
        const std::string tmp_relpath = relpath + ".tmp";
        const int tmp_fd = ::openat(
            parent_dirfd,
            tmp_relpath
                .c_str(),  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace tmp open — effects::IO, held by the record_event Subrow<Row<IO,Block>> boundary that reaches this static helper on the Bg persistence thread
            O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (tmp_fd < 0) return false;

        // The nested scope closes the writer before the renameat below,
        // which is what POSIX recommends for atomic-rename writes and keeps
        // no extra descriptor open across the rename window.
        {
            crucible::fixy::handle::FileHandle write_guard{tmp_fd};

            std::size_t off = 0;
            while (off < bytes.size()) {
                const ssize_t r = ::write(
                    tmp_fd,  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace bytes write — effects::IO, held by the record_event Subrow<Row<IO,Block>> persistence boundary
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
                    tmp_fd);  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace flush blocks on disk — effects::IO + effects::Block, held by the Subrow<Row<IO,Block>> persistence boundary
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
                parent_dirfd);  // SYSCALL-CAP-OK: Cipher cold-tier atomic-replace parent-dir fsync blocks on disk — effects::IO + effects::Block, held by the record_event Subrow<Row<IO,Block>> persistence boundary
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
    [[nodiscard]] static bool atomic_append_at_(int parent_dirfd, const std::string& relpath,
                                                std::span<const std::uint8_t> bytes) noexcept {
        const int fd = ::openat(
            parent_dirfd,
            relpath
                .c_str(),  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append log open — effects::IO, held by the record_event/persist_session_events Subrow<Row<IO,Block>> persistence boundary on the Bg thread
            O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (fd < 0) return false;

        {
            crucible::fixy::handle::FileHandle guard{fd};

            // With O_APPEND and a record smaller than a block, ::write
            // either fails outright or writes every byte.  The loop covers
            // a short write on signal, and each re-issue is a fresh
            // kernel-atomic O_APPEND write.
            std::size_t off = 0;
            while (off < bytes.size()) {
                const ssize_t r = ::write(
                    fd,  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append record write — effects::IO, held by the record_event/persist_session_events Subrow<Row<IO,Block>> persistence boundary
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
                    fd);  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append flush blocks on disk — effects::IO + effects::Block, held by the Subrow<Row<IO,Block>> persistence boundary
            } while (frc < 0 && errno == EINTR);
            if (frc != 0) return false;
        }

        // Makes the file's existence durable for the first record.
        int drc;
        do {
            drc = ::fsync(
                parent_dirfd);  // SYSCALL-CAP-OK: Cipher cold-tier atomic-append parent-dir fsync blocks on disk — effects::IO + effects::Block, held by the record_event/persist_session_events Subrow<Row<IO,Block>> persistence boundary
        } while (drc < 0 && errno == EINTR);
        return drc == 0;
    }

    // Returns a closed FileHandle on failure.  O_DIRECTORY makes the open
    // fail with ENOTDIR if a regular file has been substituted for the
    // directory, and O_NOFOLLOW rejects a symlinked leaf.
    [[nodiscard]] static crucible::fixy::handle::FileHandle open_dir_at_(int parent_dirfd,
                                                                         const char* relpath) noexcept {
        const int dfd = ::openat(
            parent_dirfd,
            relpath,  // SYSCALL-CAP-OK: Cipher cold-tier session-subdir open — effects::IO, held by the persist_session_events Subrow<Row<IO,Block>> persistence boundary that reaches this static helper on the Bg thread
            O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (dfd < 0) return crucible::fixy::handle::FileHandle{};
        return crucible::fixy::handle::FileHandle{dfd};
    }

    [[nodiscard]] auto obj_path(uint64_t hash) const
        -> ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::CipherPath> {
        char hex[16];
        hex16_(hash, hex);
        const std::string& root = root_str();
        if (root.size() > MAX_ROOT_PATH_BYTES) {
            std::abort();
        }
        std::string path{root};
        path.reserve(root.size() + OBJECT_PATH_SUFFIX_BYTES);
        path.append("/objects/");
        path.append(hex, 2);
        path.push_back('/');
        path.append(hex + 2, 14);
        return ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::CipherPath>{std::move(path)};
    }

    // Returns "objects/<XX>/<14hex>".  No leading slash: openat resolves a
    // relative path under the parent dirfd's inode, not under "/".
    [[nodiscard]] static auto obj_relpath_(std::uint64_t hash)
        -> ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::CipherPath> {
        char hex[16];
        hex16_(hash, hex);
        std::string r;
        r.reserve(sizeof("objects/") - 1 + 2 + 1 + 14);
        r.append("objects/");
        r.append(hex, 2);
        r.push_back('/');
        r.append(hex + 2, 14);
        return ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::CipherPath>{std::move(r)};
    }

    std::string session_event_dir(crucible::fixy::sess::eventlog::SessionTagId session) const {
        char hex[16];
        hex16_(session.value, hex);
        const std::string& root = root_str();
        std::string path{root};
        path.reserve(root.size() + sizeof("/session_events/") - 1 + 16);
        path.append("/session_events/");
        path.append(hex, 16);
        return path;
    }

    static std::string session_event_dir_relpath_(crucible::fixy::sess::eventlog::SessionTagId session) {
        char hex[16];
        hex16_(session.value, hex);
        std::string path;
        path.reserve(sizeof("session_events/") - 1 + 16);
        path.append("session_events/");
        path.append(hex, 16);
        return path;
    }

    std::string session_event_batch_path(crucible::fixy::sess::eventlog::SessionTagId session, ContentHash hash) const {
        char hex[16];
        hex16_(hash.raw(), hex);
        std::string path = session_event_dir(session);
        path.push_back('/');
        path.append(hex, 16);
        path.append(".cfed");
        return path;
    }

    [[nodiscard]] std::span<const uint8_t> cached_bytes(ContentHash hash) const noexcept {
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

    void remember_cached_bytes(ContentHash hash, std::span<const uint8_t> bytes) const {
        if (!hash || bytes.empty() || bytes.size() > MAX_RESIDENT_CACHE_BYTES) {
            return;
        }
        for (const CachedObjectBytes& entry : resident_cache_) {
            if (entry.hash == hash) return;
        }

        if (resident_cache_.capacity() < MAX_RESIDENT_CACHE_ENTRIES) {
            resident_cache_.reserve(MAX_RESIDENT_CACHE_ENTRIES);
        }
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

    static void hex16_(uint64_t value, char (&out)[16]) noexcept {
        static constexpr char kHex[] = "0123456789abcdef";
        for (std::size_t i = 0; i < 16; ++i) {
            out[15 - i] = kHex[value & 0x0FULL];
            value >>= 4;
        }
    }

    static void write_u64_dec_(std::ofstream& out, uint64_t value) {
        char buf[20];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
        if (ec == std::errc{}) {
            out.write(buf, ptr - buf);
        }
    }

    [[nodiscard]] static bool file_bytes_equal_(const std::string& path, std::span<const std::uint8_t> expected) {
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

    [[nodiscard]] static ContentHash session_event_payload_hash_(std::span<const std::uint8_t> bytes) noexcept {
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

    // std::from_chars, not std::stoull: stoull throws on malformed input,
    // which is undefined under -fno-exceptions.  Returns true only when the
    // whole [begin, end) range parsed as a number in the given base.
    [[nodiscard]] static bool parse_u64(const char* begin, const char* end, int base, uint64_t& out) noexcept {
        if (begin >= end) return false;
        auto [p, ec] = std::from_chars(begin, end, out, base);
        return ec == std::errc{} && p == end;
    }

    [[nodiscard]] static bool parse_session_index_line_(std::string_view line, uint64_t& first, uint64_t& last,
                                                        uint64_t& count, uint64_t& hash) noexcept {
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

    // Malformed lines are skipped rather than fatal.  A corrupt or
    // truncated log leaves the in-memory state as a proper prefix of what
    // was on disk instead of killing the process.
    void load_log() {
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

            log_.emplace(LogEntry::cipher_store_committed(crucible::fixy::sess::eventlog::StepId{step_id},
                                                          ContentHash{raw_hash}, ts_ns));
        }
    }

    // Conservative upper bound on the serialized size of a RegionNode.
    //
    // The three unconditional terms sum to 352 bytes, so the result is
    // positive by construction.  Stating that in the return type means a
    // refactor that drops the headroom or makes a header conditional trips
    // the contract here, instead of silently handing the caller a
    // zero-length buffer to serialize into.
    static crucible::fixy::wrap::Positive<std::size_t> estimate_serial_size(const RegionNode* region) {
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
        return crucible::fixy::wrap::Positive<std::size_t>{sz + 256};  // headroom
    }

    // steady_clock on Linux is CLOCK_MONOTONIC, which is the right source
    // for ordering commit events within one process run.  It freezes through
    // suspend and is immune to the NTP back-jumps that would scramble the
    // log's ordering invariant.
    [[nodiscard]] static auto now_ns() noexcept -> ::crucible::fixy::time::MonotonicClockBytes<std::uint64_t> {
        const auto tp = std::chrono::steady_clock::now();
        const std::uint64_t raw = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
        return ::crucible::fixy::time::mint_clock_source<::crucible::fixy::time::ClockSource_v::Monotonic,
                                                         std::uint64_t>(raw);
    }

    static_assert(std::is_same_v<decltype(now_ns()), ::crucible::fixy::time::MonotonicClockBytes<std::uint64_t>>,
                  "Cipher::now_ns must return MonotonicClockBytes<u64>.");
    static_assert(sizeof(decltype(now_ns())) == sizeof(std::uint64_t), "MonotonicClockBytes must be a zero-cost wrap.");
    static_assert(decltype(now_ns())::source == ::crucible::fixy::time::ClockSource_v::Monotonic,
                  "Cipher commit timestamp provenance must be Monotonic.");
};

// Type-level witness that a caller validated a head hash at its source.
// Zero is the sentinel for "no commit yet", so a zero head is
// indistinguishable from a step before the first commit and corrupts the
// binary search in hash_at_step.
//
// This catches the value where it is produced.  The precondition on
// advance_head catches it at the function boundary.  Both layers stay,
// because either alone can be bypassed.
using ValidCipherHead = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::non_zero, ContentHash>;

[[nodiscard, gnu::const]] inline constexpr ContentHash make_cipher_head(ValidCipherHead raw) noexcept {
    return raw.value();
}

static_assert(crucible::fixy::wrap::no_scoped_view_field_check<Cipher>());
static_assert(sizeof(Cipher::ContentAddressedRegionPayload) == sizeof(const RegionNode*));
static_assert(crucible::fixy::sess::contentaddr::is_content_addressed_v<
              typename Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(crucible::fixy::sess::contentaddr::is_content_addressed_v<
              typename Cipher::LoadedContentAddressedRegionPayload::payload_type>);

}  // namespace crucible
