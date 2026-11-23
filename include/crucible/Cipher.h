#pragma once

// Content-addressed object store for Merkle DAG nodes.
//
// On-disk layout:
//   $root/objects/<first2hex>/<remaining14hex>  — one file per node
//   $root/HEAD                                  — hex string: current active hash + "\n"
//   $root/log                                   — append-only: "step_id,hash_hex,order\n"
//
// order is the place of the commit in the order of the commits of the store:
// one for the first commit, and one more for each commit after it.  The store
// keeps the count and reads no clock, so two runs of one computation write the
// same bytes.  A time for a person goes to the sink of fixy/os/TimingSink.h.
//
// The log is the record of the commits, and HEAD is a copy of its last
// commit.  A commit reaches the log before HEAD, so HEAD never names a
// commit that the log does not hold.  A failed or interrupted HEAD write
// leaves HEAD behind the log, so the open takes the head from the log, and
// it reads HEAD only when the log holds no commit.
//
// A Cipher is not thread-safe.  One thread owns it.
//
// The members that read or write files, and the members that encode a
// region or a session-event batch, are in src/Cipher.cpp.  This header
// keeps the members that a context parameterizes, and the small members.

#include <crucible/LoadError.h>
#include <crucible/MerkleDag.h>
#include <crucible/Types.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/os/Path.h>
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
#include <foundation/effects/Capability.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/os/PathText.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible {

struct RegionNode;
class Cipher;

namespace cipher {

namespace federation {
class FederationKey;
}  // namespace federation

template <typename T>
class ContentAddressedPayload;

template <typename T>
[[nodiscard]] constexpr ContentAddressedPayload<T>
content_addressed_payload(::fixy::Option<::fixy::Ref<const T>> value) noexcept;

// A Ref of a value that the store names by its content, or no value.  The
// one door is content_addressed_payload, so every site that claims a value
// is content-addressed is a call of that name.
template <typename T>
class [[nodiscard]] ContentAddressedPayload {
public:
    using value_type = T;
    using payload_type = ::fixy::session::ContentAddressed<T>;
    using Value = ::fixy::Option<::fixy::Ref<const T>>;

    [[nodiscard]] constexpr Value get() const noexcept { return value_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_.is_some(); }

private:
    constexpr explicit ContentAddressedPayload(Value value) noexcept : value_(value) {}

    template <typename U>
    friend constexpr ContentAddressedPayload<U>
    content_addressed_payload(::fixy::Option<::fixy::Ref<const U>> value) noexcept;

    Value value_{};
};

// A loaded value: a Ref of the value, and where its bytes came from.  Only
// the store builds one, because the cache flag is a claim about where the
// bytes came from.  A load that refuses its bytes gives a LoadError in place
// of this value.
template <typename T>
class [[nodiscard]] LoadedContentAddressedPayload {
public:
    using value_type = T;
    using payload_type = ::fixy::session::ContentAddressed<T>;
    using Value = ::fixy::Ref<T>;

    [[nodiscard]] constexpr Value get() const noexcept { return value_; }
    [[nodiscard]] constexpr bool cache_hit() const noexcept { return cache_hit_; }

private:
    constexpr LoadedContentAddressedPayload(Value value, bool cache_hit) noexcept
        : value_(value), cache_hit_(cache_hit) {}

    friend class ::crucible::Cipher;

    // No initializer: a Ref has no default value, and the one constructor
    // sets it.
    Value value_;
    bool cache_hit_ = false;
};

template <typename T>
[[nodiscard]] constexpr ContentAddressedPayload<T>
content_addressed_payload(::fixy::Option<::fixy::Ref<const T>> value) noexcept {
    return ContentAddressedPayload<T>{value};
}

// A region whose lifetime band is at least Scope.  A commit entry point
// persists into a tier that lives as long as its scope, so a narrower
// band would leak its region into storage that outlives it.  The band
// holds a Ref of the region, so a band holds a region in each case.
template <typename W, ::fixy::Lifetime_v Scope>
concept LifetimePinnedRegion = ::fixy::is_band_of_v<::fixy::LifetimeLattice, W> && ::fixy::satisfies_v<W, Scope>
                            && std::convertible_to<::fixy::band_value_t<W>, ::fixy::Ref<const RegionNode>>;

// The gate of each operation of the store, the commit to the head log too,
// is CtxFitsCipherPersistence of crucible/cipher/SessionPersistenceSurface.h,
// so that a header that names the gate and no member of the store does not
// include this header.

}  // namespace cipher

class CRUCIBLE_OWNER Cipher {
public:
    // The bound of the root is the bound of a sanitized path, so a root
    // that the sanitizer passes fits RootPath.
    static constexpr std::size_t MAX_ROOT_PATH_BYTES = ::fixy::kPathBytes;

    // The path of the root, with a zero after its last character.
    using RootPath = ::foundation::os::PathText<MAX_ROOT_PATH_BYTES>;

    // HEAD holds sixteen hexadecimal digits and a newline.  A longer HEAD is
    // corrupt, so the open reads at most this many bytes of it.
    static constexpr std::size_t MAX_HEAD_BYTES = 32;

    using ContentAddressedRegionPayload = cipher::ContentAddressedPayload<RegionNode>;
    using LoadedContentAddressedRegionPayload = cipher::LoadedContentAddressedPayload<RegionNode>;
    using SessionEvent = ::fixy::session::SessionEvent;

    using persist_session_events_required_row = ::crucible::CipherSessionEventPersistenceRow;

    static_assert(sizeof(SessionEvent) == ::fixy::session::session_event_size && sizeof(SessionEvent) == 72,
                  "Cipher session-event persistence is pinned to the SessionEvent cold-tier wire size.");

    // The payload of a region.  A Ref is never null, so the payload holds the
    // region.  The empty payload comes from cipher::content_addressed_payload
    // with no value.
    [[nodiscard]] static ContentAddressedRegionPayload
    content_addressed(::fixy::Ref<const RegionNode> region) noexcept {
        return cipher::content_addressed_payload<RegionNode>(ContentAddressedRegionPayload::Value::some(region));
    }

    // The text of a root.  The root comes from an operator, so it crosses a
    // trust boundary, and the caller tags it External.
    using RootText = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::External>;

    // The context of the file calls of the store: the context of the
    // caller, narrowed to the row of the calls.  A context that fits the
    // store has the capability Bg, Init or Test, so the context is one of
    // three types.  The members of src/Cipher.cpp take it, and each file
    // call there reads the context that the caller gave.
    using FileCtx =
        ::fixy::OneOf<::foundation::effects::ExecCtx<::foundation::effects::Bg, persist_session_events_required_row>,
                      ::foundation::effects::ExecCtx<::foundation::effects::Init, persist_session_events_required_row>,
                      ::foundation::effects::ExecCtx<::foundation::effects::Test, persist_session_events_required_row>>;

    // The reason that open gives no store.  A root is configuration that an
    // operator gives, so a wrong root can occur, and the caller reports the
    // reason.
    enum class OpenError : std::uint8_t {
        // The sanitizer refuses the text: it is empty, longer than
        // MAX_ROOT_PATH_BYTES, or it holds a zero byte or a ".." part.
        path_refused,
        // A part of the root does not exist, and the open cannot make it,
        // for example a part that is a link to a path that does not exist.
        missing,
        // A part of the root is no directory, or the root itself is a link.
        not_a_directory,
        // The process may not make or open a part of the root.
        no_permission,
        // The make or the open of a part fails for another reason.
        open_failed,
    };

    // The text of a reason, for a report to a person.
    [[nodiscard]] static ::fixy::TextView open_error_text(OpenError reason) noexcept;

    // The reason that persist_session_events stores no batch.  Each value
    // is a condition that the store tests.
    enum class PersistRefusal : std::uint8_t {
        // The batch holds no event.
        empty_batch,
        // The batch holds more than MAX_SESSION_EVENT_BATCH_EVENTS events.
        batch_above_limit,
        // The events of the batch belong to more than one session.
        mixed_sessions,
        // The step ids of the events are not consecutive.
        steps_not_consecutive,
        // The federation entry of the batch does not encode.
        entry_encode_failed,
        // The directory of the session does not open, or its sync fails.
        session_directory_failed,
        // The store holds a batch file of the hash with different bytes.
        stored_batch_differs,
        // The write or the flush of the batch file fails.
        batch_write_failed,
        // The append or the flush of the index line fails.
        index_append_failed,
    };

    // The text of a refusal, for a report to a person.
    [[nodiscard]] static ::fixy::TextView persist_refusal_text(PersistRefusal reason) noexcept;

    // The reason that persist_session_events gives no hash: a refusal of
    // the store, or the LoadError of a batch file that the store holds
    // under the hash and that does not read.
    using PersistError = ::fixy::OneOf<PersistRefusal, LoadError>;
    using PersistResult = ::fixy::Result<ContentHash, PersistError>;

    // The open call creates the root and its object directory and reads
    // HEAD and the log.  Its context must admit IO and Block, and the read
    // of the log takes the allocation capability.  A root that the open
    // cannot make or open gives its reason.
    //
    // The path check here is string-level only.  The operator trusts the
    // links of the root path, so the open follows the link of each parent,
    // as mkdir -p does.  The open of the root itself folds in O_NOFOLLOW, so
    // a root that names a link gives not_a_directory.  A slash at the end of
    // the root makes the kernel follow that link.  Symlink defense lives in
    // the doors under the root directory, which src/Cipher.cpp calls.  Each door
    // resolves its path beneath the root, and each open of a leaf folds in
    // O_NOFOLLOW.
    //
    // The return type is CipherOpenResult, below the class.  A Result checks
    // its payload type when a declaration names it, and the class is not
    // complete here, so the body gives the type.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[gnu::cold]] static auto open(Ctx const& ctx, ::foundation::effects::Alloc allocation, RootText root_text) {
        using Opened = ::fixy::Result<Cipher, OpenError>;
        const ::fixy::Result<::fixy::Path<::fixy::tags::source::Sanitized>, ::fixy::PathTraversalError> root =
            ::fixy::sanitize_path(root_text);
        return root.match(
            [&](::fixy::Path<::fixy::tags::source::Sanitized> const& sanitized_root) -> Opened {
                Cipher c{allocation};
                CRUCIBLE_TRY(_, c.open_root_(file_ctx_(ctx), allocation, sanitized_root));
                return c;
            },
            [](::fixy::PathTraversalError const&) -> Opened { return ::fixy::err(OpenError::path_refused); });
    }

    // A closed store that holds the allocation capability of its caller.
    // open builds one and then opens its root, and it gives the store only
    // when the root opens.
    explicit Cipher(::foundation::effects::Alloc allocation) noexcept : allocation_{allocation} {}
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

    // The door gives the view on the erased identity through erase_brand.
    // The brand of mint_view names the door and not the caller.  The call of
    // mint_view names no template argument of the door, so each context and
    // each Cipher get the same brand (foundation/Brand.h, fact 2).  The rule
    // brand-escape refuses a fresh brand that leaves the door, and the
    // receivers of the view have their bodies in src/Cipher.cpp, so they take
    // the one view type.  A brand for each Cipher waits for the brand of an
    // arena or a store.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] OpenView mint_open_view(Ctx const&) const noexcept {
        // CRUCIBLE_PRE rather than a pre() clause: on this toolchain a pre()
        // predicate that reads a member through `this` is silently skipped
        // at consteval.  Every in-body precondition of the Cipher, here and
        // in src/Cipher.cpp, has the same reason.
        CRUCIBLE_PRE(is_open());
        return ::fixy::erase_brand(::fixy::mint_view<cipher_state::Open>(*this));
    }

    [[nodiscard]] friend constexpr bool view_ok(Cipher const& c, std::type_identity<cipher_state::Open>) noexcept {
        return c.is_open();
    }

    // Writes the region to its object file, flushes it, and returns its
    // content hash.  A failure gives the none hash.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] ContentHash store(Ctx const& ctx, OpenView const& view, ContentAddressedRegionPayload payload) {
        return store_(file_ctx_(ctx), view, payload);
    }

    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] ::fixy::wait::Block<ContentHash> store_pinned(Ctx const& ctx, OpenView const& view,
                                                                ContentAddressedRegionPayload payload) {
        return ::fixy::mint_band<::fixy::wait::Block<ContentHash>>(store(ctx, view, payload));
    }

    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] ::fixy::cipher_tier::Warm<ContentHash> publish_warm(Ctx const& ctx, OpenView const& view,
                                                                      ContentAddressedRegionPayload payload) {
        return ::fixy::mint_band<::fixy::cipher_tier::Warm<ContentHash>>(store(ctx, view, payload));
    }

    // The type declares Hot residency, but nothing is replicated.  The
    // returned none hash lets a caller testing the hash tell that no
    // Hot-tier store happened.
    [[nodiscard]] ::fixy::cipher_tier::Hot<ContentHash> publish_hot(OpenView const&,
                                                                    ContentAddressedRegionPayload payload) noexcept;

    // The type declares Cold residency, but nothing is written to durable
    // storage.  The returned none hash lets a caller testing the hash tell
    // that no Cold-tier store happened.
    [[nodiscard]] ::fixy::cipher_tier::Cold<ContentHash> publish_cold(OpenView const&,
                                                                      ContentAddressedRegionPayload payload) noexcept;

    // The commit_per_* family pairs a declared data lifetime with the
    // storage tier that matches it.  The rejection direction is the point:
    // a request-scoped value does not satisfy the fleet-scoped requirement,
    // so commit_per_fleet refuses it and request-scoped state cannot leak
    // into durable fleet-wide storage.

    template <typename W>
        requires cipher::LifetimePinnedRegion<W, ::fixy::Lifetime_v::PER_REQUEST>
    [[nodiscard]] ::fixy::cipher_tier::Hot<ContentHash> commit_per_request(OpenView const& view,
                                                                           W lifetime_pinned_region) noexcept {
        return publish_hot(view, content_addressed(std::move(lifetime_pinned_region).consume()));
    }

    template <typename Ctx, typename W>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
              && cipher::LifetimePinnedRegion<W, ::fixy::Lifetime_v::PER_PROGRAM>
    [[nodiscard]] ::fixy::cipher_tier::Warm<ContentHash> commit_per_program(Ctx const& ctx, OpenView const& view,
                                                                            W lifetime_pinned_region) {
        return publish_warm(ctx, view, content_addressed(std::move(lifetime_pinned_region).consume()));
    }

    template <typename W>
        requires cipher::LifetimePinnedRegion<W, ::fixy::Lifetime_v::PER_FLEET>
    [[nodiscard]] ::fixy::cipher_tier::Cold<ContentHash> commit_per_fleet(OpenView const& view,
                                                                          W lifetime_pinned_region) noexcept {
        return publish_cold(view, content_addressed(std::move(lifetime_pinned_region).consume()));
    }

    // Real regions sit at a megabyte or less.  The ceiling leaves room for
    // mixture-of-experts and long-horizon traces while rejecting a corrupt
    // or adversarial length that would make the loader allocate SIZE_MAX.
    static constexpr size_t MAX_OBJECT_BYTES = size_t{256} << 20;

    using ValidatedObjectSize = ::fixy::Refined<::fixy::bounded_above<MAX_OBJECT_BYTES>, size_t>;

    // The result of a load: the loaded region, or the reason that the store
    // gives no region of the hash.
    using LoadResult = ::fixy::Result<LoadedContentAddressedRegionPayload, LoadError>;

    // const even though it writes the resident cache: the cache is
    // process-local acceleration for the content-addressed quotient, not
    // durable Cipher state.  A hit materializes from bytes already seen
    // under this hash and touches no filesystem.  A refusal names its
    // reason: the none hash, an object file that does not open or has a
    // size that the load refuses, the first fault of the image, or a region
    // whose content hash is not the hash of its object.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] LoadResult load_content_addressed(Ctx const& ctx, OpenView const& view,
                                                    ::foundation::effects::Alloc a, ContentHash content_hash,
                                                    ::fixy::BumpArena<DagArena>& arena) const {
        return load_content_addressed_(file_ctx_(ctx), view, a, content_hash, arena);
    }

    // The context is what keeps a foreground caller out.  record_event
    // writes HEAD and appends to the log, so it needs IO and Block.  A
    // hot-path context holds neither, and performing file I/O there would
    // break replay determinism.  The row names the effects of the commit.
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

    // The largest step of a commit.  A step counts the regions of a run one
    // at a time, so a real run does not get near it.  The ceiling leaves room
    // for the step after the last commit, where a new run starts.  A log line
    // with a larger step is corrupt.
    static constexpr std::uint64_t MAX_COMMIT_STEP = (std::uint64_t{1} << 62U) - 1U;

    // The step of a commit that failed, or the reason that refused it.
    enum class CommitError : std::uint8_t {
        // The place of the last commit is the largest count, so the next
        // commit has no place.  Only a log line from the disk gives that
        // place.
        order_exhausted,
        log_append,
        head_write,
        // The step is below the step of the last commit of the store.  The
        // commit writes nothing and takes no place.
        step_order,
        // The step is above MAX_COMMIT_STEP.  The commit writes nothing and
        // takes no place.
        step_above_limit,
    };

    using CommitResult = ::fixy::Result<::fixy::Unit, CommitError>;

    // Appends the commit of content_hash at step_id to the log, with the
    // next place of the count of the commits, and then writes HEAD.  The
    // steps of the log do not go down: a step below the last commit gives
    // step_order, and a step above MAX_COMMIT_STEP gives step_above_limit.
    // A second process can open a store of an earlier run, so these are
    // errors of the commit and not contracts.  The
    // commit reads no clock, because a clock time in the log makes two runs
    // of one computation write two logs.  So the gate asks only for the row
    // of the store, and the context needs no Nondet.
    //
    // The log append is the commit.  A failed append moves no head, in
    // memory or in HEAD, but it takes its place.  A log write that passed
    // before a failed flush can leave the record in the file, and a reopen
    // then reads that commit, so the next commit needs a place of its own.  A
    // failed HEAD write comes after a durable append, so the commit stands:
    // the head moves, and only HEAD lags the log.  Each failure gives the
    // step that failed.
    //
    // content_hash must be non-zero.  Zero is the sentinel for "no content",
    // so a step recorded with hash zero is indistinguishable from "before
    // the first commit" and corrupts the binary search in hash_at_step.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] CommitResult record_event(Ctx const& ctx, OpenView const& view, ContentHash content_hash,
                                            uint64_t step_id) {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return advance_head(file_ctx_(ctx), view, content_hash, step_id);
    }

    // Stores one batch of session events and indexes it, and gives the
    // content hash of the batch.  The events must belong to one session and
    // carry consecutive step ids.  A refused or failed batch gives its
    // reason.  A batch file of the hash that the store holds already and
    // that does not read gives the LoadError of its read.  The encode of the
    // batch takes the allocation capability.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] PersistResult persist_session_events(Ctx const& ctx, OpenView const& view,
                                                       ::foundation::effects::Alloc allocation,
                                                       ::fixy::View<SessionEvent const> events) {
        return persist_session_events_(file_ctx_(ctx), view, allocation, events);
    }

    // The events of a load, in step order, and the number of batches of the
    // index that the load skips.  A batch is skipped when its file does not
    // read or when it fails a check, and the load reports each skip with
    // its LoadError.  A second index line of a batch that the load holds is
    // no skip, because a writer that stores a batch again appends its line
    // again.  A batch whose steps overlap the events of the load is a skip.
    struct SessionEventLoad {
        ::foundation::core::GrowList<SessionEvent> events =
            ::foundation::core::mint_grow_list<SessionEvent>(::fixy::max_of<std::size_t>);
        std::size_t skipped_batches = 0;
    };

    // The events of one session from the step from_step on, in step order.
    // The read of the index takes the allocation capability.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] SessionEventLoad
    load_session_events(Ctx const& ctx, OpenView const& view, ::foundation::effects::Alloc a,
                        ::fixy::session::SessionTagId session, ::fixy::session::StepId from_step = {}) const {
        return load_session_events_(file_ctx_(ctx), view, a, session, from_step);
    }

    [[nodiscard]] ContentHash hash_at_step(OpenView const&, uint64_t step_id) const;

    [[nodiscard]] ContentHash head() const { return head_; }
    [[nodiscard]] bool empty() const { return !head_; }

    // The step of the last commit of the log, or none when the log holds no
    // commit.  A run that opens the store of an earlier run starts its steps
    // after it, so its commits keep the order of the log.  The step is at
    // most MAX_COMMIT_STEP.
    [[nodiscard]] ::fixy::Option<::fixy::session::StepId> last_committed_step() const noexcept {
        if (log_.empty()) return ::fixy::none;
        return ::fixy::some(log_.back().step_id());
    }

    // The text of the root is a member of the store.  The deleted `const&&`
    // twin refuses a read of the root of a temporary store, because the
    // store releases that text at the end of the statement.
    // get checks that the root is set, in each build.
    [[nodiscard]] ::fixy::TextView root() const& { return root_.get().value().c_text().view(); }
    auto root() const&& = delete("a temporary store releases its root at the end of the statement. Read the root "
                                 "of a store that has a name");

    // The content hash that names a session-event batch and its index
    // line.  Anyone who writes the store can compute it, so it proves
    // only which bytes a batch holds, and the loader checks each record
    // after it.  Complexity: linear in the payload size.
    [[nodiscard]] static ContentHash session_event_batch_hash(::fixy::View<std::byte const> bytes) noexcept;

    // The federation key in the header of a session-event batch: the keyed
    // hash of the payload under the key of the batches.  That key is a
    // constant of the store and names no secret.  It keeps a batch apart
    // from each other kind of entry, so an entry of another kind does not
    // load as a batch.  Complexity: linear in the payload size.
    [[nodiscard]] static cipher::federation::FederationKey
    session_event_batch_key(::fixy::View<std::byte const> bytes) noexcept;

private:
    using LogEntry = ::fixy::session::SessionEvent;

    static_assert(sizeof(LogEntry) == 72);

    struct CachedObjectBytes {
        ContentHash hash;
        ::fixy::HeapArray<std::byte> bytes{};
    };

    static constexpr size_t MAX_RESIDENT_CACHE_BYTES = size_t{8} << 20;
    static constexpr size_t MAX_RESIDENT_CACHE_ENTRIES = 64;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES = size_t{64} << 20;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_EVENTS =
        MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES / sizeof(SessionEvent);

    ::fixy::WriteOnce<::fixy::Tagged<RootPath, ::fixy::tags::source::Durable>> root_ =
        ::fixy::mint_write_once<::fixy::Tagged<RootPath, ::fixy::tags::source::Durable>>();
    // The directory handle of the Cipher root.  Every helper that touches
    // the on-disk tree opens relative to this handle with O_NOFOLLOW, which
    // buys three properties:
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
    // O_NOFOLLOW inspects only the trailing pathname component.  Each door
    // under the dirfd also resolves its path beneath the root, so a
    // symbolic link in an intermediate directory, an absolute path or a
    // ".." part cannot take the path out of the root.  A link that stays
    // inside the root is followed.  Substituting an intermediate directory
    // with such a link needs write access to the Cipher root, which is
    // outside the threat model: whoever owns this dirfd also owns the
    // root's permissions.
    ::fixy::fs::DirectoryHandle root_directory_{};
    ContentHash head_{};

    ::fixy::OrderedAppendOnly<LogEntry, ::fixy::session::StepIdKeyFn, ::fixy::session::StepIdLess> log_ =
        ::fixy::mint_ordered_append_only<LogEntry, ::fixy::session::StepIdKeyFn, ::fixy::session::StepIdLess>();
    // The place of the last commit that the store wrote or read: zero
    // before the first commit.  Each commit takes the next place, and the
    // open continues from the place of the last line of the log.
    ::fixy::Monotonic<std::uint64_t> commits_ = ::fixy::mint_monotonic<std::uint64_t>(0);
    // The entries in the order of use, least recent first.  The capacity is
    // the entry limit, so the cache holds its slots in the Cipher and a
    // push never grows a buffer.
    mutable ::fixy::FixedList<CachedObjectBytes, MAX_RESIDENT_CACHE_ENTRIES> resident_cache_{};
    mutable size_t resident_cache_bytes_ = 0;
    // The allocation capability that the constructor received.  The image of
    // a store and the copies of the resident cache allocate only through it.
    // It has no initializer, because an allocation tag has no public default
    // constructor, and the one constructor sets it.
    [[no_unique_address]] ::foundation::effects::Alloc allocation_;

    [[nodiscard]] static constexpr bool commits_head_(const LogEntry& entry) noexcept {
        return ::fixy::session::session_op_commits_cipher_head(entry.op());
    }

    [[nodiscard]] static constexpr ContentHash committed_hash_(const LogEntry& entry) noexcept {
        return ContentHash{entry.cipher_content().value};
    }

    [[nodiscard]] ContentHash latest_committed_head() const noexcept;

    // The context of a caller as a FileCtx.  The gate of the caller admits
    // IO and Block, so the narrow keeps the capability of the context.
    template <typename Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard]] static FileCtx file_ctx_(Ctx const& ctx) noexcept {
        return ctx.template in_row<persist_session_events_required_row>();
    }

    [[nodiscard]] ContentHash store_(FileCtx const& file_ctx, OpenView const&, ContentAddressedRegionPayload payload);

    [[nodiscard]] LoadResult load_content_addressed_(FileCtx const& file_ctx, OpenView const&,
                                                     ::foundation::effects::Alloc a, ContentHash content_hash,
                                                     ::fixy::BumpArena<DagArena>& arena) const;

    [[nodiscard]] SessionEventLoad load_session_events_(FileCtx const& file_ctx, OpenView const&,
                                                        ::foundation::effects::Alloc a,
                                                        ::fixy::session::SessionTagId session,
                                                        ::fixy::session::StepId from_step) const;

    // The commit that record_event starts.  The commit takes the next place
    // of commits_, and the in-memory event and the log line each hold that
    // place.
    [[nodiscard]] CommitResult advance_head(FileCtx const& file_ctx, OpenView const&, ContentHash content_hash,
                                            uint64_t step_id);

    [[nodiscard]] PersistResult persist_session_events_(FileCtx const& file_ctx, OpenView const&,
                                                        ::foundation::effects::Alloc allocation,
                                                        ::fixy::View<SessionEvent const> events);

    // The head hash that the text of a HEAD file names, or none when the
    // text before its first newline is no hexadecimal number.
    [[nodiscard]] static ::fixy::Option<ContentHash> parse_head_(::fixy::TextView text) noexcept;

    // The bytes of the cached image of hash, or no bytes.  The View borrows
    // the cache until the next remember_cached_bytes.
    [[nodiscard]] ::fixy::View<const std::byte> cached_bytes(ContentHash hash) const noexcept;

    void remember_cached_bytes(ContentHash hash, ::fixy::View<const std::byte> bytes) const;

    // Makes the root and its object directory, opens the root, and reads
    // the log and HEAD.  The make starts at "/" for an absolute root, and
    // at the current directory for a relative root, and it makes one name
    // at a time.  So an absolute root does not read the current directory.
    // A root that the make cannot make, or that does not open as a
    // directory, gives its reason.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, OpenError>
    open_root_(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation,
               ::fixy::Path<::fixy::tags::source::Sanitized> const& root);

    // Malformed lines are skipped rather than fatal.  A corrupt or
    // truncated log leaves the in-memory state as a proper prefix of what
    // was on disk instead of killing the process.  The steps and the places
    // of the commits only grow, so a line whose step goes back, or whose
    // place does not grow, is a corrupt line too.
    void load_log(FileCtx const& file_ctx, ::foundation::effects::Alloc allocation);
};

// The result of Cipher::open: an open store, or the reason that the open
// gives no store.
using CipherOpenResult = ::fixy::Result<Cipher, Cipher::OpenError>;

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
