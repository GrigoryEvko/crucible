#pragma once

// Content-addressed object store for Merkle DAG nodes.
//
// On-disk layout:
//   $root/objects/<first2hex>/<remaining14hex>  — one file per node
//   $root/HEAD                                  — hex string: current active hash + "\n"
//   $root/log                                   — append-only: "step_id,hash_hex,ts_ns\n"
//
// ts_ns is a reading of CLOCK_MONOTONIC in nanoseconds.
//
// A Cipher is not thread-safe.  One thread owns it.
//
// The members that read or write files, and the members that encode a
// region or a session-event batch, are in src/Cipher.cpp.  This header
// keeps the members that a context parameterizes, and the small members.

#include <crucible/Types.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <fixy/Bands.h>
#include <fixy/Mutation.h>
#include <fixy/Path.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Time.h>
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

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <inplace_vector>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible {

class Arena;
struct MetaLog;
struct RegionNode;
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

// The gate of a commit to the head log.  The commit writes and flushes
// files, so the context must fit the store.  The commit also stamps the
// log entry with a reading of the monotonic clock, so the context must
// fit the clock reader: its row owns Bg, Init or Test.  A clock read on
// the replay-bound foreground path makes replay diverge across machines.
template <typename Ctx>
concept CtxFitsCipherCommit = ::crucible::CtxFitsCipherPersistence<Ctx>
                           && ::fixy::time::CtxFitsClockReaderMint<Ctx, ::fixy::ClockSource_v::Monotonic>;

}  // namespace cipher

class CRUCIBLE_OWNER Cipher {
public:
    static constexpr std::size_t MAX_ROOT_PATH_BYTES = 4096;

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
    // the O_NOFOLLOW-anchored openat helpers of src/Cipher.cpp.
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
        // hash.  A corrupt HEAD falls through to the log.
        bool head_from_file = false;
        auto head_file = head_sanitized ? ::fixy::fs::mint_file<::fixy::fs::read_only>(ctx, std::move(*head_sanitized))
                                        : std::expected<::fixy::Linear<::fixy::fs::OwnedFd>, std::error_code>{
                                              std::unexpected{std::make_error_code(std::errc::invalid_argument)}};
        if (head_file) {
            const ::fixy::fs::OwnedFd head_fd = std::move(*head_file).consume();
            std::array<char, 32> buf{};
            const auto n = ::fixy::fs::read_full(ctx, head_fd, std::as_writable_bytes(std::span<char>{buf}));
            if (n && *n > 0) {
                const std::optional<ContentHash> parsed_head = parse_head_(std::span<const char>{buf.data(), *n});
                if (parsed_head) {
                    c.head_ = *parsed_head;
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
        // at consteval.  Every in-body precondition of the Cipher, here and
        // in src/Cipher.cpp, has the same reason.
        CRUCIBLE_PRE(is_open());
        return ::fixy::mint_view<cipher_state::Open>(*this);
    }

    [[nodiscard]] friend constexpr bool view_ok(Cipher const& c, std::type_identity<cipher_state::Open>) noexcept {
        return c.is_open();
    }

    // Writes the region to its object file, flushes it, and returns its
    // content hash.  A failure gives the none hash.
    [[nodiscard]] ContentHash store(OpenView const&, ContentAddressedRegionPayload payload, const MetaLog* meta_log);

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
    publish_hot(OpenView const&, ContentAddressedRegionPayload payload, const MetaLog* meta_log) noexcept;

    // The type declares Cold residency, but nothing is written to durable
    // storage.  The returned none hash lets a caller testing the hash tell
    // that no Cold-tier store happened.
    [[nodiscard]] ::fixy::cipher_tier::Cold<ContentHash>
    publish_cold(OpenView const&, ContentAddressedRegionPayload payload, const MetaLog* meta_log) noexcept;

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
                                                                             Arena& arena) const;

    // The context is what keeps a foreground caller out.  record_event
    // writes HEAD and appends to the log, so it needs IO and Block.  A
    // hot-path context holds neither, and performing file I/O there would
    // break replay determinism.  The row names the effects of the commit.
    // The clock read of the commit is a second gate, in CtxFitsCipherCommit.
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
    static_assert(::foundation::effects::row_size(^^record_event_required_row) == 2u,
                  "record_event_required_row must be exactly 2 atoms (IO + Block).");
    static_assert(::foundation::effects::row_contains(^^record_event_required_row, ::foundation::effects::Effect::IO),
                  "record_event_required_row must contain Effect::IO.  It is the "
                  "fence's reason for existence: HEAD and log file writes.");
    static_assert(::foundation::effects::row_contains(^^record_event_required_row,
                                                      ::foundation::effects::Effect::Block),
                  "record_event_required_row must contain Effect::Block.  File writes "
                  "block on the kernel.");
    static_assert(std::is_same_v<persist_session_events_required_row, record_event_required_row>,
                  "Session-event persistence uses the same IO+Block row fence "
                  "as Cipher::record_event.");

    // Moves the head to content_hash at step_id, and appends the commit to
    // the log with a reading of the monotonic clock.  The reader comes from
    // the context, so the reading keeps its clock type until the log entry
    // encodes it.  A failed clock read changes nothing and gives the error
    // of the read.
    //
    // content_hash must be non-zero.  Zero is the sentinel for "no content",
    // so a step recorded with hash zero is indistinguishable from "before
    // the first commit" and corrupts the binary search in hash_at_step.
    template <typename Ctx>
        requires cipher::CtxFitsCipherCommit<Ctx>
    [[nodiscard]] std::expected<void, std::error_code> record_event(Ctx const& ctx, OpenView const& view,
                                                                    ContentHash content_hash, uint64_t step_id) {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        const ::fixy::time::MonotonicClock clock =
            ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
        auto committed_at = clock.read();
        if (!committed_at) [[unlikely]] {
            return std::unexpected{committed_at.error()};
        }
        advance_head(view, content_hash, step_id, *committed_at);
        CRUCIBLE_POST(0, head_ == content_hash);
        return {};
    }

    // Stores one batch of session events and indexes it.  The events must
    // belong to one session and carry consecutive step ids.  A refused or
    // failed batch gives the none hash.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] ContentHash persist_session_events(Ctx const&, OpenView const& view,
                                                     std::span<const SessionEvent> events) {
        return persist_session_events_(view, events);
    }

    [[nodiscard]] std::vector<SessionEvent> load_session_events(OpenView const&, ::fixy::session::SessionTagId session,
                                                                ::fixy::session::StepId from_step = {}) const;

    [[nodiscard]] ContentHash hash_at_step(OpenView const&, uint64_t step_id) const;

    [[nodiscard]] ContentHash head() const { return head_; }
    [[nodiscard]] bool empty() const { return !head_; }
    [[nodiscard]] const std::string& root() const CRUCIBLE_LIFETIMEBOUND { return root_str(); }

    // The content hash that names a session-event batch and its index
    // line.  Anyone who writes the store can compute it, so it proves
    // only which bytes a batch holds, and the loader checks each record
    // after it.  Complexity: linear in the payload size.
    [[nodiscard]] static ContentHash session_event_batch_hash(std::span<const std::uint8_t> bytes) noexcept;

private:
    using LogEntry = ::fixy::session::SessionEvent;

    static_assert(sizeof(LogEntry) == 72);

    struct CachedObjectBytes {
        ContentHash hash;
        std::vector<uint8_t> bytes;
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
    // The entries in the order of use, least recent first.  The capacity is
    // the entry limit, so the cache holds its slots in the Cipher and a
    // push never grows a buffer.
    mutable std::inplace_vector<CachedObjectBytes, MAX_RESIDENT_CACHE_ENTRIES> resident_cache_;
    mutable size_t resident_cache_bytes_ = 0;

    [[nodiscard]] static constexpr bool commits_head_(const LogEntry& entry) noexcept {
        return ::fixy::session::session_op_commits_cipher_head(entry.op());
    }

    [[nodiscard]] static constexpr ContentHash committed_hash_(const LogEntry& entry) noexcept {
        return ContentHash{entry.cipher_content().value};
    }

    [[nodiscard]] ContentHash latest_committed_head() const noexcept;

    // The commit that record_event starts.  The reading is the time of the
    // commit, and a clock reader is the only source of one.  The in-memory
    // event and the log line are the two wire forms, and each takes the
    // nanosecond count of the reading.
    void advance_head(OpenView const&, ContentHash content_hash, uint64_t step_id,
                      ::fixy::MonotonicClockBytes<std::uint64_t> committed_at);

    [[nodiscard]] ContentHash persist_session_events_(OpenView const&, std::span<const SessionEvent> events);

    // The head hash that a HEAD file names, or none when the bytes do not
    // start with a hexadecimal number.
    [[nodiscard]] static std::optional<ContentHash> parse_head_(std::span<const char> bytes) noexcept;

    [[nodiscard]] auto obj_path(uint64_t hash) const -> ::fixy::Tagged<std::string, ::fixy::tags::source::CipherPath>;

    [[nodiscard]] std::string session_event_dir(::fixy::session::SessionTagId session) const;

    [[nodiscard]] std::string session_event_batch_path(::fixy::session::SessionTagId session, ContentHash hash) const;

    [[nodiscard]] std::span<const uint8_t> cached_bytes(ContentHash hash) const noexcept;

    void remember_cached_bytes(ContentHash hash, std::span<const uint8_t> bytes) const;

    // Malformed lines are skipped rather than fatal.  A corrupt or
    // truncated log leaves the in-memory state as a proper prefix of what
    // was on disk instead of killing the process.
    void load_log();
};

// Type-level witness that a caller validated a head hash at its source.
// Zero is the sentinel for "no commit yet", so a zero head is
// indistinguishable from a step before the first commit and corrupts the
// binary search in hash_at_step.
//
// This catches the value where it is produced.  The precondition on
// record_event catches it at the function boundary.  Both layers stay,
// because either alone can be bypassed.
using ValidCipherHead = ::fixy::Refined<::fixy::non_zero, ContentHash>;

[[nodiscard, gnu::const]] inline constexpr ContentHash make_cipher_head(ValidCipherHead raw) noexcept {
    return raw.value();
}

}  // namespace crucible
