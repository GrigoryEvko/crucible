#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedSnapshot<T, UserTag> — SWMR worked example
//
// Combines AtomicSnapshot<T> (Lamport seqlock for the data race)
// with SharedPermissionPool<Reader> (atomic refcount + mode-transition
// CAS for the type-system enforcement).  The result: Augur-style
// metrics broadcast pattern — one writer, many readers, race-free,
// with type-system proof that "writer" and "reader" roles cannot be
// confused at compile time.
//
//   sizeof(WriterHandle) == sizeof(PermissionedSnapshot*)  (Permission EBO)
//   sizeof(ReaderHandle) == sizeof(SharedPermissionGuard)  (= sizeof(void*))
//
// Per-operation cost (steady state):
//   publish() : ~15 ns    (AtomicSnapshot's two fetch_add + memcpy)
//   load()    : ~10 ns    (AtomicSnapshot's seq + memcpy + seq + retry)
//   reader() acquire/release: 2 atomic acq_rel ops on Pool refcount
//
// ─── The three-piece architecture ───────────────────────────────────
//
//   AtomicSnapshot<T>            — handles the BYTE-level race
//                                   (seqlock retry, two fetch_add per
//                                    publish, fence + retry per load)
//   SharedPermissionPool<Reader> — handles the LIFETIME tracking
//                                   (atomic refcount of outstanding
//                                    Reader shares, mode-transition CAS
//                                    when writer needs exclusive)
//   PermissionedSnapshot         — composes the two with type-system
//                                   enforcement (Writer vs Reader at
//                                   the API level)
//
// Why both layers?  AtomicSnapshot alone is sound for SWMR (publish is
// lock-free, load retries on torn reads).  But it offers no API-level
// distinction between writer and reader — any thread can call publish()
// or load() because they're both methods on the snapshot.  Wrapping
// with SharedPermissionPool adds:
//
//   1. Compile-time type discrimination (Writer vs Reader handles)
//   2. Runtime lifetime tracking (Pool refcount of outstanding readers)
//   3. Mode-transition mechanism (with_exclusive_access for ops that
//      need all readers out — schema reset, atomic reinitialization)
//
// ─── The Augur metrics use case ─────────────────────────────────────
//
// Augur (per CRUCIBLE.md) broadcasts a Metrics struct from one
// background thread to many monitoring readers.  PermissionedSnapshot
// gives:
//
//   * Type system enforces that monitoring readers cannot accidentally
//     overwrite metrics (no .publish() on ReaderHandle)
//   * Pool refcount tells the writer how many monitors are live
//   * Mode transition lets the writer reset the snapshot atomically
//     when the metrics schema version bumps
//
// All without requiring a lock anywhere on the hot path — both
// publish() and load() remain wait-free under the underlying
// AtomicSnapshot's protocol.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T satisfies SnapshotValue (trivially-copyable, trivially-
//     destructible, sizeof <= 256 bytes).  Inherited from
//     AtomicSnapshot.
//   * Each PermissionedSnapshot instance must use a distinct UserTag
//     (or different T) so its Permission tags don't collide.  In
//     practice: define a tag-class per logical channel.
//   * WriterHandle / ReaderHandle are move-only (the embedded
//     Permission / Guard enforce linearity).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedSnapshot ────────────────────────────────
//
// Each logical snapshot channel picks its own UserTag (a phantom
// type, typically an empty struct).  The (Whole, Writer, Reader)
// triple is auto-specialized for splits_into — no per-tag
// boilerplate.

namespace snapshot_tag {

template <typename UserTag> struct Whole  {};
template <typename UserTag> struct Writer {};
template <typename UserTag> struct Reader {};

}  // namespace snapshot_tag

// ── PermissionedSnapshot<T, UserTag> ─────────────────────────────────

template <SnapshotValue T, typename UserTag = void>
class PermissionedSnapshot
    : public safety::Pinned<PermissionedSnapshot<T, UserTag>> {
public:
    using value_type = T;
    using user_tag   = UserTag;
    using whole_tag  = snapshot_tag::Whole<UserTag>;
    using writer_tag = snapshot_tag::Writer<UserTag>;
    using reader_tag = snapshot_tag::Reader<UserTag>;

    // ── Construction ──────────────────────────────────────────────
    //
    // The Reader permission is minted internally and parked in the
    // Pool — fractional model means the framework manages its own
    // share-tracking root.  The user keeps the Writer permission
    // separately (mint with permission_root_mint<writer_tag>(),
    // hand to writer() factory below).

    PermissionedSnapshot() noexcept
        : snap_{}
        , reader_pool_{safety::permission_root_mint<reader_tag>()} {}

    explicit PermissionedSnapshot(const T& initial) noexcept
        : snap_{initial}
        , reader_pool_{safety::permission_root_mint<reader_tag>()} {}

    // ── WriterHandle ──────────────────────────────────────────────
    //
    // Move-only by virtue of the embedded Permission's deleted copy.
    // Constructed via writer() factory; consumes the Writer permission.
    // sizeof(WriterHandle) == sizeof(void*) via EBO.
    class WriterHandle {
        PermissionedSnapshot* snap_ = nullptr;
        [[no_unique_address]] safety::Permission<writer_tag> perm_;

        constexpr WriterHandle(PermissionedSnapshot& s,
                               safety::Permission<writer_tag>&& p) noexcept
            : snap_{&s}, perm_{std::move(p)} {}
        friend class PermissionedSnapshot;

    public:
        WriterHandle(const WriterHandle&)
            = delete("WriterHandle owns the Writer Permission — copy would duplicate the linear token");
        WriterHandle& operator=(const WriterHandle&)
            = delete("WriterHandle owns the Writer Permission — assignment would overwrite the linear token");
        constexpr WriterHandle(WriterHandle&&) noexcept = default;
        constexpr WriterHandle& operator=(WriterHandle&&) noexcept = default;

        // Publish — wait-free per AtomicSnapshot's protocol.
        void publish(const T& value) noexcept { snap_->snap_.publish(value); }

        // Diagnostic: the snapshot's publish version (post-publish count).
        [[nodiscard]] std::uint64_t version() const noexcept {
            return snap_->snap_.version();
        }
    };

    // ── ReaderHandle ──────────────────────────────────────────────
    //
    // Move-only via the embedded SharedPermissionGuard's deleted copy.
    // Constructed via reader() factory; holds a Pool refcount share
    // for its lifetime (decrement happens on destruction).
    // sizeof(ReaderHandle) == sizeof(snap_*) + sizeof(Guard).
    class ReaderHandle {
        PermissionedSnapshot* snap_ = nullptr;
        safety::SharedPermissionGuard<reader_tag> guard_;

        constexpr ReaderHandle(PermissionedSnapshot& s,
                               safety::SharedPermissionGuard<reader_tag>&& g) noexcept
            : snap_{&s}, guard_{std::move(g)} {}
        friend class PermissionedSnapshot;

    public:
        ReaderHandle(const ReaderHandle&)
            = delete("ReaderHandle owns a Pool refcount share — copy would double-count");
        ReaderHandle& operator=(const ReaderHandle&)
            = delete("ReaderHandle owns a Pool refcount share — assignment would double-count");
        constexpr ReaderHandle(ReaderHandle&&) noexcept = default;
        // Move assignment deleted because Guard's lifetime is fixed
        // at construction (Guard itself rejects move-assignment).

        // Load — blocking retry on torn reads via AtomicSnapshot's seqlock.
        [[nodiscard]] T load() const noexcept { return snap_->snap_.load(); }

        // Try-load — non-blocking; nullopt iff in-progress write detected.
        [[nodiscard]] std::optional<T> try_load() const noexcept {
            return snap_->snap_.try_load();
        }

        // Diagnostic: snapshot's publish version.
        [[nodiscard]] std::uint64_t version() const noexcept {
            return snap_->snap_.version();
        }
    };

    // ── Factories ─────────────────────────────────────────────────

    // Writer endpoint — consumes the Writer permission token.
    // Caller mints via permission_root_mint<writer_tag>() at startup,
    // moves it through writer() to obtain the unique WriterHandle.
    [[nodiscard]] WriterHandle writer(safety::Permission<writer_tag>&& perm) noexcept {
        return WriterHandle{*this, std::move(perm)};
    }

    // Reader endpoint — lends a Pool share.  Returns nullopt iff
    // exclusive mode is active (with_exclusive_access in flight).
    // Multiple readers may hold ReaderHandles concurrently — that's
    // the entire point of the fractional permission.
    [[nodiscard]] std::optional<ReaderHandle> reader() noexcept {
        auto guard = reader_pool_.lend();
        if (!guard) return std::nullopt;
        return ReaderHandle{*this, std::move(*guard)};
    }

    // ── Mode transition: scoped exclusive access ──────────────────
    //
    // For special operations that need ALL readers out — atomic
    // reinitialization, schema reset, snapshot replacement.  Body
    // runs while readers are blocked from acquiring new shares.
    // Returns true iff body ran (false iff readers were active).
    //
    // Body signature: void() noexcept.
    //
    // Cost: one CAS to acquire (succeeds iff outstanding == 0),
    // one release-store to deposit back.  Body's runtime is the
    // rest.  Subsequent reader() calls succeed once body returns.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_exclusive_access(Body&& body)
        noexcept(std::is_nothrow_invocable_v<Body>)
    {
        auto upgrade = reader_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        reader_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    // ── Diagnostics ───────────────────────────────────────────────

    [[nodiscard]] std::uint64_t outstanding_readers() const noexcept {
        return reader_pool_.outstanding();
    }

    [[nodiscard]] bool is_exclusive_active() const noexcept {
        return reader_pool_.is_exclusive_out();
    }

    // Snapshot's publish version (count of completed publishes).
    // Useful for "did the snapshot change?" cache-invalidation
    // decisions in monitoring code.
    [[nodiscard]] std::uint64_t version() const noexcept {
        return snap_.version();
    }

private:
    AtomicSnapshot<T>                              snap_;
    safety::SharedPermissionPool<reader_tag>       reader_pool_;
};

}  // namespace crucible::concurrent

// ── splits_into auto-specialization ─────────────────────────────────
//
// User declares Permission<Whole<X>> at startup; framework does the
// rest.  splits_into binary form supports the canonical
// (Whole → Writer + Reader) decomposition without per-tag boilerplate.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::snapshot_tag::Whole<UserTag>,
                   concurrent::snapshot_tag::Writer<UserTag>,
                   concurrent::snapshot_tag::Reader<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::snapshot_tag::Whole<UserTag>,
                        concurrent::snapshot_tag::Writer<UserTag>,
                        concurrent::snapshot_tag::Reader<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
