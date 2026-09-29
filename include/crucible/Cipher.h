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
#include <fixy/Bands.h>
#include <fixy/Mutation.h>
#include <fixy/Path.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/Fs.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/EventLog.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
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

class Cipher;

namespace cipher {

template <typename T>
class ContentAddressedPayload;

template <typename T>
[[nodiscard]] constexpr ContentAddressedPayload<T> content_addressed_payload(const T* value) noexcept;

// A pointer to a value that the store names by its content.  The one
// door is content_addressed_payload, so every site that claims a value
// is content-addressed is a call of that name.
template <typename T>
class [[nodiscard]] ContentAddressedPayload {
public:
    using value_type = T;
    using payload_type = ::fixy::session::ContentAddressed<T>;

    [[nodiscard]] constexpr const T* get() const noexcept { return value_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != nullptr; }

private:
    constexpr explicit ContentAddressedPayload(const T* value) noexcept : value_(value) {}

    template <typename U>
    friend constexpr ContentAddressedPayload<U> content_addressed_payload(const U* value) noexcept;

    const T* value_ = nullptr;
};

// The result of a load.  An empty result is a null pointer.  Only the
// store builds a result that holds a value, because the cache flag is a
// claim about where the bytes came from.
template <typename T>
class [[nodiscard]] LoadedContentAddressedPayload {
public:
    using value_type = T;
    using payload_type = ::fixy::session::ContentAddressed<T>;

    constexpr LoadedContentAddressedPayload(std::nullptr_t) noexcept {}

    [[nodiscard]] constexpr T* get() const noexcept { return value_; }
    [[nodiscard]] constexpr bool cache_hit() const noexcept { return cache_hit_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != nullptr; }
    [[nodiscard]] constexpr operator T*() const noexcept { return value_; }

private:
    constexpr LoadedContentAddressedPayload(T* value, bool cache_hit) noexcept : value_(value), cache_hit_(cache_hit) {}

    friend class ::crucible::Cipher;

    T* value_ = nullptr;
    bool cache_hit_ = false;
};

template <typename T>
[[nodiscard]] constexpr ContentAddressedPayload<T> content_addressed_payload(const T* value) noexcept {
    return ContentAddressedPayload<T>{value};
}

// A region whose lifetime band is at least Scope.  A commit entry point
// persists into a tier that lives as long as its scope, so a narrower
// band would leak its region into storage that outlives it.
template <typename W, ::fixy::Lifetime_v Scope>
concept LifetimePinnedRegion = ::fixy::is_band_of_v<::fixy::LifetimeLattice, W> && ::fixy::satisfies_v<W, Scope>
                            && std::convertible_to<::fixy::band_value_t<W>, const RegionNode*>;

}  // namespace cipher

class CRUCIBLE_OWNER Cipher {
public:
    static constexpr std::size_t MAX_ROOT_PATH_BYTES = 4096;
    static constexpr std::size_t OBJECT_PATH_SUFFIX_BYTES = sizeof("/objects/") - 1 + 2 + 1 + 14;

    using ContentAddressedRegionPayload = cipher::ContentAddressedPayload<RegionNode>;
    using LoadedContentAddressedRegionPayload = cipher::LoadedContentAddressedPayload<RegionNode>;
    using SessionEvent = ::fixy::session::SessionEvent;

    using persist_session_events_required_row = ::crucible::CipherSessionEventPersistenceRow;

    inline static constexpr RowHash SESSION_EVENT_FEDERATION_ROW_HASH{
        ::foundation::diag::row_hash_contribution_v<persist_session_events_required_row>};

    static_assert(static_cast<bool>(SESSION_EVENT_FEDERATION_ROW_HASH));
    static_assert(sizeof(SessionEvent) == ::fixy::session::session_event_size && sizeof(SessionEvent) == 72,
                  "Cipher session-event persistence is pinned to the SessionEvent cold-tier wire size.");

    [[nodiscard]] static constexpr ContentAddressedRegionPayload content_addressed(const RegionNode* region) noexcept {
        return cipher::content_addressed_payload(region);
    }

    // The open call creates the object directory and reads HEAD and the
    // log.  Its context must admit IO and Block.
    //
    // The path check here is string-level only.  Symlink defense lives in
    // the O_NOFOLLOW-anchored openat helpers below.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[gnu::cold]] static Cipher open(Ctx const& ctx, ::fixy::Path<::fixy::tags::source::External> root_external) {
        Cipher c;

        auto sanitized_e = ::fixy::sanitize_path(std::move(root_external));
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
            c.root_.try_set(::fixy::mint_tagged<::fixy::tags::source::Durable>(root));
        [[assume(root_set)]];
        std::filesystem::create_directories(root + "/objects");

        // The root passed the sanitizer and holds no `..`, so the HEAD path
        // under it passes too.
        auto head_sanitized = ::fixy::sanitize_path(
            ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{root} / "HEAD"));

        // The root opens with O_NOFOLLOW, so a symlinked root fails with
        // ELOOP.  Abort rather than continue: the durable-on-return
        // contract needs a root directory whose identity is stable for
        // this instance's lifetime.
        {
            auto dirfd = ::fixy::fs::open_dirfd(ctx, std::move(*sanitized_e));
            if (!dirfd) std::abort();
            c.root_dirfd_ = std::move(*dirfd);
        }

        c.load_log();

        // HEAD is the authoritative pointer and overrides the log's last
        // hash.  std::from_chars is exception-free, unlike std::stoull which
        // throws on malformed input, so a corrupt HEAD falls through to the
        // log.
        bool head_from_file = false;
        auto head_file = head_sanitized ? ::fixy::fs::mint_file<::fixy::fs::read_only>(ctx, std::move(*head_sanitized))
                                        : std::expected<::fixy::Linear<::fixy::fs::OwnedFd>, std::error_code>{
                                              std::unexpected{std::make_error_code(std::errc::invalid_argument)}};
        if (head_file) {
            const ::fixy::fs::OwnedFd head_fd = std::move(*head_file).consume();
            std::array<char, 32> buf{};
            const auto n = ::fixy::fs::read_full(ctx, head_fd, std::as_writable_bytes(std::span<char>{buf}));
            if (n && *n > 0) {
                uint64_t raw = 0;
                const char* begin = buf.data();
                const char* end = buf.data() + *n;
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
    Cipher& operator=(Cipher&&) = delete(
        "Cipher holds a write-once root and an append-only log.  An assignment would drop both; move-construct "
        "a new Cipher instead");

    [[nodiscard]] constexpr bool is_open() const noexcept { return root_.has_value(); }

    using OpenView = ::crucible::CipherOpenView;

    // Every write through an open view writes object files and flushes them
    // to storage, so the view needs a context whose row admits IO and Block.
    // A hot foreground context holds neither and gets no view.
    using open_view_required_row = ::crucible::CipherSessionEventPersistenceRow;

    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] OpenView mint_open_view(Ctx const&) const noexcept {
        // CRUCIBLE_PRE rather than a pre() clause: on this toolchain a pre()
        // predicate that reads a member through `this` is silently skipped
        // at consteval.  Every in-body precondition in this file has the
        // same reason.
        CRUCIBLE_PRE(is_open());
        return ::fixy::mint_view<cipher_state::Open>(*this);
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

    [[nodiscard]] ::fixy::wait::Block<ContentHash>
    store_pinned(OpenView const& view, ContentAddressedRegionPayload payload, const MetaLog* meta_log) {
        return ::fixy::mint_band<::fixy::wait::Block<ContentHash>>(store(view, payload, meta_log));
    }

    [[nodiscard]] ::fixy::cipher_tier::Warm<ContentHash>
    publish_warm(OpenView const& view, ContentAddressedRegionPayload payload, const MetaLog* meta_log) {
        return ::fixy::mint_band<::fixy::cipher_tier::Warm<ContentHash>>(store(view, payload, meta_log));
    }

    // The type declares Hot residency, but nothing is replicated.  The
    // returned none hash lets a caller testing the hash tell that no
    // Hot-tier store happened.
    [[nodiscard]] ::fixy::cipher_tier::Hot<ContentHash>
    publish_hot(OpenView const&, ContentAddressedRegionPayload /*payload*/, const MetaLog* /*meta_log*/) noexcept {
        return cipher::mint_promote<::fixy::CipherTierTag_v::Cold, ::fixy::CipherTierTag_v::Hot>(
            ::fixy::mint_band<::fixy::cipher_tier::Cold<ContentHash>>(ContentHash{}));
    }

    // The type declares Cold residency, but nothing is written to durable
    // storage.  The returned none hash lets a caller testing the hash tell
    // that no Cold-tier store happened.
    [[nodiscard]] ::fixy::cipher_tier::Cold<ContentHash>
    publish_cold(OpenView const&, ContentAddressedRegionPayload /*payload*/, const MetaLog* /*meta_log*/) noexcept {
        return cipher::mint_demote<::fixy::CipherTierTag_v::Hot, ::fixy::CipherTierTag_v::Cold>(
            ::fixy::mint_band<::fixy::cipher_tier::Hot<ContentHash>>(ContentHash{}));
    }

    // The commit_per_* family pairs a declared data lifetime with the
    // storage tier that matches it.  The rejection direction is the point:
    // a request-scoped value does not satisfy the fleet-scoped requirement,
    // so commit_per_fleet refuses it and request-scoped state cannot leak
    // into durable fleet-wide storage.

    template <typename W>
        requires cipher::LifetimePinnedRegion<W, ::fixy::Lifetime_v::PER_REQUEST>
    [[nodiscard]] ::fixy::cipher_tier::Hot<ContentHash>
    commit_per_request(OpenView const& view, W lifetime_pinned_region, const MetaLog* meta_log) noexcept {
        const RegionNode* region = std::move(lifetime_pinned_region).consume();
        return publish_hot(view, content_addressed(region), meta_log);
    }

    template <typename W>
        requires cipher::LifetimePinnedRegion<W, ::fixy::Lifetime_v::PER_PROGRAM>
    [[nodiscard]] ::fixy::cipher_tier::Warm<ContentHash>
    commit_per_program(OpenView const& view, W lifetime_pinned_region, const MetaLog* meta_log) {
        const RegionNode* region = std::move(lifetime_pinned_region).consume();
        return publish_warm(view, content_addressed(region), meta_log);
    }

    template <typename W>
        requires cipher::LifetimePinnedRegion<W, ::fixy::Lifetime_v::PER_FLEET>
    [[nodiscard]] ::fixy::cipher_tier::Cold<ContentHash>
    commit_per_fleet(OpenView const& view, W lifetime_pinned_region, const MetaLog* meta_log) noexcept {
        const RegionNode* region = std::move(lifetime_pinned_region).consume();
        return publish_cold(view, content_addressed(region), meta_log);
    }

    // Real regions sit at a megabyte or less.  The ceiling leaves room for
    // mixture-of-experts and long-horizon traces while rejecting a corrupt
    // or adversarial length that would make the loader allocate SIZE_MAX.
    static constexpr size_t MAX_OBJECT_BYTES = size_t{256} << 20;

    using ValidatedObjectSize = ::fixy::Refined<::fixy::bounded_above<MAX_OBJECT_BYTES>, size_t>;

    // const even though it writes the resident cache: the cache is
    // process-local acceleration for the content-addressed quotient, not
    // durable Cipher state.  A hit materializes from bytes already seen
    // under this hash and touches no filesystem.
    [[nodiscard]] LoadedContentAddressedRegionPayload load_content_addressed(OpenView const&,
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

        const std::optional<LoadedRegionNode> loaded_region =
            deserialize_region(a, std::span<const uint8_t>{buf}, arena);
        // The file name is the key, and anyone who writes the store can put
        // any region under any name.  A region whose own hash is not the key
        // is refused, the same as on the cache path above, so a lookup never
        // returns another region.
        if (!loaded_region || loaded_region->value()->content_hash != content_hash) return nullptr;
        remember_cached_bytes(content_hash, std::span<const uint8_t>{buf});
        return LoadedContentAddressedRegionPayload{loaded_region->value(), false};
    }

    // content_hash must be non-zero.  Zero is the sentinel for "no content",
    // so a step recorded with hash zero is indistinguishable from "before
    // the first commit" and corrupts the binary search in hash_at_step.
    void advance_head(OpenView const&, ContentHash content_hash, uint64_t step_id)
        pre(::foundation::decide::is_non_zero(content_hash)) {
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

        const uint64_t ts = now_ns();
        log_.append(SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted,
                                               ::fixy::session::StepId{step_id},
                                               ::fixy::session::StateHash{content_hash.raw()}, ts));
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
        CRUCIBLE_POST(0, log_.back().step_id().value == step_id);
    }

    // The context is what keeps a foreground caller out.  record_event
    // writes HEAD and appends to the log, so it needs IO and Block.  A
    // hot-path context holds neither, and performing file I/O there would
    // break replay determinism.
    using record_event_required_row =
        ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

    // The asserts below pin the exact row contents.  Narrowing the alias to
    // Row<IO> would still compile and still reject an empty row, so it would
    // silently drop the Block half of the fence without these.
    static_assert(
        std::is_same_v<record_event_required_row, ::foundation::effects::Row<::foundation::effects::Effect::IO,
                                                                             ::foundation::effects::Effect::Block>>,
        "Cipher::record_event_required_row must be exactly Row<IO, Block>.  "
        "Adding or removing an atom changes the fence every call site "
        "depends on.");
    static_assert(::foundation::effects::row_size_v<record_event_required_row> == 2u,
                  "record_event_required_row must be exactly 2 atoms (IO + Block).");
    static_assert(
        ::foundation::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete-row static_assert (record_event_required_row), not a Ctx capability check
                record_event_required_row, ::foundation::effects::Effect::IO>,
        "record_event_required_row must contain Effect::IO.  It is the "
        "fence's reason for existence: HEAD and log file writes.");
    static_assert(
        ::foundation::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete-row static_assert (record_event_required_row), not a Ctx capability check
                record_event_required_row, ::foundation::effects::Effect::Block>,
        "record_event_required_row must contain Effect::Block.  File writes "
        "block on the kernel.");
    static_assert(std::is_same_v<persist_session_events_required_row, record_event_required_row>,
                  "Session-event persistence uses the same IO+Block row fence "
                  "as Cipher::record_event.");

    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    void record_event(Ctx const&, OpenView const& view, ContentHash content_hash, uint64_t step_id) {
        advance_head(view, content_hash, step_id);
    }

    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] ContentHash persist_session_events(Ctx const&, OpenView const&,
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

        if (!atomic_append_at_(session_dirfd.get(), "index",
                               std::span<const std::uint8_t>{
                                   static_cast<const std::uint8_t*>(static_cast<const void*>(idx_rec)), off})) {
            return ContentHash{};
        }

        return hash;
    }

    [[nodiscard]] std::vector<SessionEvent> load_session_events(OpenView const&, ::fixy::session::SessionTagId session,
                                                                ::fixy::session::StepId from_step = {}) const {
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

            out.reserve(out.size() + decoded->size());
            for (const SessionEvent& event : *decoded) {
                if (event.step_id().value >= from_step.value) {
                    out.push_back(event);
                    highest_loaded = event.step_id().value;
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

    [[nodiscard]] ContentHash head() const { return head_; }
    [[nodiscard]] bool empty() const { return !head_; }
    [[nodiscard]] const std::string& root() const CRUCIBLE_LIFETIMEBOUND { return root_str(); }

    // The content hash that names a session-event batch and its index
    // line.  Anyone who writes the store can compute it, so it proves
    // only which bytes a batch holds, and the loader checks each record
    // after it.  Complexity: linear in the payload size.
    [[nodiscard]] static ContentHash session_event_batch_hash(std::span<const std::uint8_t> bytes) noexcept {
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

private:
    using LogEntry = ::fixy::session::SessionEvent;

    static_assert(sizeof(LogEntry) == 72);

    struct CachedObjectBytes {
        ContentHash hash;
        std::vector<uint8_t> bytes;
    };

    // One descriptor that a helper below opened relative to a directory
    // descriptor.  open_at_ is the one door: it builds an owner only from
    // the value that ::openat returned, and the destructor closes it.
    class AnchoredFd {
        int fd_ = -1;

        constexpr explicit AnchoredFd(int fd) noexcept : fd_{fd} {}

        friend class Cipher;

    public:
        AnchoredFd(const AnchoredFd&) = delete("a descriptor is unique; a copy would close it twice");
        AnchoredFd& operator=(const AnchoredFd&) = delete("a descriptor is unique; a copy would close it twice");
        AnchoredFd(AnchoredFd&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}
        AnchoredFd& operator=(AnchoredFd&&) = delete("an owner is built once, by open_at_, and never reassigned");

        ~AnchoredFd() {
            if (fd_ >= 0) {
                ::close(
                    fd_);  // SYSCALL-CAP-OK: Cipher::AnchoredFd releases the descriptor that open_at_ acquired under the CtxFitsCipherPersistence gate of the open view
            }
        }

        [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
        [[nodiscard]] int get() const noexcept { return fd_; }
    };

    static constexpr size_t MAX_RESIDENT_CACHE_BYTES = size_t{8} << 20;
    static constexpr size_t MAX_RESIDENT_CACHE_ENTRIES = 64;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES = size_t{64} << 20;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_EVENTS =
        MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES / sizeof(SessionEvent);

    ::fixy::WriteOnce<::fixy::Tagged<std::string, ::fixy::tags::source::Durable>> root_ =
        ::fixy::mint_write_once<::fixy::Tagged<std::string, ::fixy::tags::source::Durable>>();
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
    ::fixy::fs::Dirfd root_dirfd_{};
    ContentHash head_{};

    [[nodiscard]] const std::string& root_str() const noexcept {
        [[assume(root_.has_value())]];
        return root_.get_assuming_set().value();
    }
    ::fixy::OrderedAppendOnly<LogEntry, ::fixy::session::StepIdKeyFn, ::fixy::session::StepIdLess> log_ =
        ::fixy::mint_ordered_append_only<LogEntry, ::fixy::session::StepIdKeyFn, ::fixy::session::StepIdLess>();
    mutable std::vector<CachedObjectBytes> resident_cache_;
    mutable size_t resident_cache_bytes_ = 0;

    [[nodiscard]] static constexpr bool commits_head_(const LogEntry& entry) noexcept {
        return ::fixy::session::session_op_commits_cipher_head(entry.op());
    }

    [[nodiscard]] static constexpr ContentHash committed_hash_(const LogEntry& entry) noexcept {
        return ContentHash{entry.cipher_content().value};
    }

    [[nodiscard]] ContentHash latest_committed_head() const noexcept {
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

    [[nodiscard]] static AnchoredFd open_at_(int parent_dirfd, const char* relpath, int flags,
                                             ::mode_t perms) noexcept {
        return AnchoredFd{::openat(
            parent_dirfd,
            relpath,  // SYSCALL-CAP-OK: Cipher cold-tier anchored open — effects::IO, held by the CtxFitsCipherPersistence gate of the open view that every caller of these static helpers holds on the persistence thread
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
    [[nodiscard]] static bool fdatasync_at_(int parent_dirfd, const std::string& relpath) noexcept {
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
    [[nodiscard]] static bool atomic_write_at_(int parent_dirfd, const std::string& relpath,
                                               std::span<const std::uint8_t> bytes) noexcept {
        const std::string tmp_relpath = relpath + ".tmp";

        // The nested scope closes the writer before the renameat below,
        // which is what POSIX recommends for atomic-rename writes and keeps
        // no extra descriptor open across the rename window.
        {
            const AnchoredFd write_guard = open_at_(parent_dirfd, tmp_relpath.c_str(),
                                                    O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
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
    [[nodiscard]] static bool atomic_append_at_(int parent_dirfd, const std::string& relpath,
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
    [[nodiscard]] static AnchoredFd open_dir_at_(int parent_dirfd, const char* relpath) noexcept {
        return open_at_(parent_dirfd, relpath, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0);
    }

    [[nodiscard]] auto obj_path(uint64_t hash) const -> ::fixy::Tagged<std::string, ::fixy::tags::source::CipherPath> {
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
        return ::fixy::mint_tagged<::fixy::tags::source::CipherPath>(std::move(path));
    }

    // Returns "objects/<XX>/<14hex>".  No leading slash: openat resolves a
    // relative path under the parent dirfd's inode, not under "/".
    [[nodiscard]] static auto obj_relpath_(std::uint64_t hash)
        -> ::fixy::Tagged<std::string, ::fixy::tags::source::CipherPath> {
        char hex[16];
        hex16_(hash, hex);
        std::string r;
        r.reserve(sizeof("objects/") - 1 + 2 + 1 + 14);
        r.append("objects/");
        r.append(hex, 2);
        r.push_back('/');
        r.append(hex + 2, 14);
        return ::fixy::mint_tagged<::fixy::tags::source::CipherPath>(std::move(r));
    }

    std::string session_event_dir(::fixy::session::SessionTagId session) const {
        char hex[16];
        hex16_(session.value, hex);
        const std::string& root = root_str();
        std::string path{root};
        path.reserve(root.size() + sizeof("/session_events/") - 1 + 16);
        path.append("/session_events/");
        path.append(hex, 16);
        return path;
    }

    static std::string session_event_dir_relpath_(::fixy::session::SessionTagId session) {
        char hex[16];
        hex16_(session.value, hex);
        std::string path;
        path.reserve(sizeof("session_events/") - 1 + 16);
        path.append("session_events/");
        path.append(hex, 16);
        return path;
    }

    std::string session_event_batch_path(::fixy::session::SessionTagId session, ContentHash hash) const {
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

    // std::from_chars, not std::stoull: stoull throws on malformed input.
    // Returns true only when the whole [begin, end) range parsed as a
    // number in the given base.
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
            // The log only grows in step order, and its append checks that
            // order with a contract that aborts.  A line out of order is a
            // corrupt line, so it is skipped like the others.
            if (!log_.empty() && step_id < log_.back().step_id().value) continue;

            log_.append(SessionEvent::cipher_event(::fixy::session::SessionOp::StoreCommitted,
                                                   ::fixy::session::StepId{step_id},
                                                   ::fixy::session::StateHash{raw_hash}, ts_ns));
        }
    }

    // Conservative upper bound on the serialized size of a RegionNode.
    //
    // The three unconditional terms sum to 352 bytes, so the result is
    // positive by construction.  Stating that in the return type means a
    // refactor that drops the headroom or makes a header conditional trips
    // the mint's precondition here, instead of silently handing the caller
    // a zero-length buffer to serialize into.
    static ::fixy::Positive<std::size_t> estimate_serial_size(const RegionNode* region) {
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

    // steady_clock on Linux is CLOCK_MONOTONIC, which is the right source
    // for ordering commit events within one process run.  It freezes through
    // suspend and is immune to the NTP back-jumps that would scramble the
    // log's ordering invariant.  The value goes into the log line and the
    // in-memory event, and nothing downstream reads it as a clock.
    [[nodiscard]] static std::uint64_t now_ns() noexcept {
        const auto tp = std::chrono::steady_clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
    }
};

// Type-level witness that a caller validated a head hash at its source.
// Zero is the sentinel for "no commit yet", so a zero head is
// indistinguishable from a step before the first commit and corrupts the
// binary search in hash_at_step.
//
// This catches the value where it is produced.  The precondition on
// advance_head catches it at the function boundary.  Both layers stay,
// because either alone can be bypassed.
using ValidCipherHead = ::fixy::Refined<::fixy::non_zero, ContentHash>;

[[nodiscard, gnu::const]] inline constexpr ContentHash make_cipher_head(ValidCipherHead raw) noexcept {
    return raw.value();
}

static_assert(::fixy::no_scoped_view_field_check<Cipher>());
static_assert(sizeof(Cipher::ContentAddressedRegionPayload) == sizeof(const RegionNode*));
static_assert(::fixy::session::is_content_addressed_v<typename Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(
    ::fixy::session::is_content_addressed_v<typename Cipher::LoadedContentAddressedRegionPayload::payload_type>);

}  // namespace crucible
