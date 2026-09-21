#pragma once

// A seqlock snapshot behind one linear Writer permission and any number
// of fractional Reader shares drawn from a pool.  The handle types
// carry the role, so a reader cannot publish.  The pool's count of
// outstanding shares is what lets the writer drain the readers and take
// the snapshot exclusively.
//
// Each snapshot needs a UserTag of its own.  Two snapshots sharing a
// tag share Permission types, and their endpoints become
// interchangeable.

#include <crucible/Platform.h>
#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/concurrent/_WorkingSet.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/safety/_Stale.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// The triple is specialized for splitting at the foot of this file, so
// a user tag takes no per-tag boilerplate.

namespace snapshot_tag {

template <typename UserTag>
struct Whole {};
template <typename UserTag>
struct Writer {};
template <typename UserTag>
struct Reader {};

}  // namespace snapshot_tag

template <SnapshotValue T, typename UserTag = void>
class PermissionedSnapshot : public safety::Pinned<PermissionedSnapshot<T, UserTag>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = snapshot_tag::Whole<UserTag>;
    using writer_tag = snapshot_tag::Writer<UserTag>;
    using reader_tag = snapshot_tag::Reader<UserTag>;

    // The reader root is minted here and parked in the pool, because
    // the fractional side tracks its own shares.  The caller mints and
    // keeps the writer permission.

    PermissionedSnapshot() noexcept : snap_{}, reader_pool_{safety::mint_permission_root<reader_tag>()} {}

    explicit PermissionedSnapshot(const T& initial) noexcept
        : snap_{initial}, reader_pool_{safety::mint_permission_root<reader_tag>()} {}

    class WriterHandle {
        PermissionedSnapshot* snap_ = nullptr;
        [[no_unique_address]] safety::Permission<writer_tag> perm_;

        constexpr WriterHandle(PermissionedSnapshot& s, safety::Permission<writer_tag>&& p) noexcept
            : snap_{&s}, perm_{std::move(p)} {}
        friend class PermissionedSnapshot;

    public:
        static constexpr std::size_t per_call_working_set = hot_path_cache_line_bytes + cell_line_footprint(sizeof(T));

        WriterHandle(const WriterHandle&) =
            delete("WriterHandle owns the Writer Permission — copy would duplicate the linear token");
        WriterHandle& operator=(const WriterHandle&) =
            delete("WriterHandle owns the Writer Permission — assignment would overwrite the linear token");
        constexpr WriterHandle(WriterHandle&&) noexcept = default;
        constexpr WriterHandle& operator=(WriterHandle&&) noexcept = default;

        void publish(const T& value) noexcept { snap_->snap_.publish(value); }

        [[nodiscard]] std::uint64_t version() const noexcept { return snap_->snap_.version(); }

        // Hands the writer permission back, so the caller can spend it
        // on a full-exclusion transition.  The rvalue qualification
        // consumes the handle at the call site, and the handle is
        // moved-from afterwards: it must not publish again.
        [[nodiscard]] safety::Permission<writer_tag> release_permission() && noexcept { return std::move(perm_); }
    };

    // Holds a pool share for its whole lifetime and gives it back on
    // destruction.
    class ReaderHandle {
        PermissionedSnapshot* snap_ = nullptr;
        safety::SharedPermissionGuard<reader_tag> guard_;

        constexpr ReaderHandle(PermissionedSnapshot& s, safety::SharedPermissionGuard<reader_tag>&& g) noexcept
            : snap_{&s}, guard_{std::move(g)} {}
        friend class PermissionedSnapshot;

    public:
        static constexpr std::size_t per_call_working_set = hot_path_cache_line_bytes + cell_line_footprint(sizeof(T));

        ReaderHandle(const ReaderHandle&) = delete("ReaderHandle owns a Pool refcount share — copy would double-count");
        ReaderHandle& operator=(const ReaderHandle&) =
            delete("ReaderHandle owns a Pool refcount share — assignment would double-count");
        constexpr ReaderHandle(ReaderHandle&&) noexcept = default;
        // Move assignment stays deleted, because the share's lifetime
        // is fixed at construction.

        [[nodiscard]] T load() const noexcept { return snap_->snap_.load(); }

        [[nodiscard]] std::optional<T> try_load() const noexcept { return snap_->snap_.try_load(); }

        [[nodiscard]] std::uint64_t version() const noexcept { return snap_->snap_.version(); }
    };

    [[nodiscard]] WriterHandle writer(safety::Permission<writer_tag>&& perm) noexcept {
        return WriterHandle{*this, std::move(perm)};
    }

    // Lends a pool share, and refuses while an exclusive transition is
    // in flight.  Several reader handles coexisting is the point of the
    // fractional side.
    [[nodiscard]] std::optional<ReaderHandle> reader() noexcept {
        auto guard = reader_pool_.lend();
        if (!guard) return std::nullopt;
        return ReaderHandle{*this, std::move(*guard)};
    }

    // Drains the readers only.  The writer permission is linear and is
    // not touched here, so the caller contract is that the thread
    // owning that permission is the one calling this.  Same-thread
    // sequencing is then what keeps a publish from overlapping the
    // body.  Any other thread calling this while the writer publishes
    // gets the ordinary seqlock behaviour, and the body runs without
    // exclusion against the writer.  with_recombined_access below is
    // the form that excludes both sides.
    //
    // Returns false when readers were still out and the body did not
    // run.  A reader can be lent again once the body returns.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = reader_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        reader_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    // Excludes both sides.  Surrendering the writer permission is the
    // proof for the writer side: while this call holds it, nothing else
    // can, so no publish can be in flight.  The pool upgrade covers the
    // readers.
    //
    // The permission comes back on every path, taken or refused, so
    // exactly one goes in and exactly one comes out.
    struct WithRecombinedResult {
        [[no_unique_address]] safety::Permission<writer_tag> writer_perm;
        bool body_ran;
    };

    template <typename Body>
        requires std::is_invocable_v<Body>
    [[nodiscard]] WithRecombinedResult with_recombined_access(safety::Permission<writer_tag>&& writer_perm,
                                                              Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = reader_pool_.try_upgrade();
        if (!upgrade) {
            return WithRecombinedResult{std::move(writer_perm), false};
        }
        std::forward<Body>(body)();
        reader_pool_.deposit_exclusive(std::move(*upgrade));
        return WithRecombinedResult{std::move(writer_perm), true};
    }

    // These two bare accessors race by design.  They read the pool's
    // atomic state with no happens-before against a concurrent lend,
    // return or upgrade, so by the time the caller looks at the value
    // readers may have come and gone.  They stay in this shape because
    // a diagnostic concept shared across the permissioned primitives
    // requires the bare scalar, and because single-threaded inspection
    // has no race to describe.

    [[nodiscard]] std::uint64_t outstanding_readers() const noexcept { return reader_pool_.outstanding(); }

    [[nodiscard]] bool is_exclusive_active() const noexcept { return reader_pool_.is_exclusive_out(); }

    // The same two values with the staleness grade attached, which
    // makes the race visible in the type and forces the caller to
    // acknowledge it.  The grade is unbounded because these accessors
    // share no step counter with the pool.
    [[nodiscard]] ::crucible::safety::Stale<std::uint64_t> outstanding_readers_stale() const noexcept {
        return ::crucible::safety::Stale<std::uint64_t>::at_infinity(reader_pool_.outstanding());
    }

    [[nodiscard]] ::crucible::safety::Stale<bool> is_exclusive_active_stale() const noexcept {
        return ::crucible::safety::Stale<bool>::at_infinity(reader_pool_.is_exclusive_out());
    }

    [[nodiscard]] std::uint64_t version() const noexcept { return snap_.version(); }

private:
    AtomicSnapshot<T> snap_;
    safety::SharedPermissionPool<reader_tag> reader_pool_;
};

}  // namespace crucible::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::snapshot_tag::Whole<UserTag>, concurrent::snapshot_tag::Writer<UserTag>,
                   concurrent::snapshot_tag::Reader<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::snapshot_tag::Whole<UserTag>, concurrent::snapshot_tag::Writer<UserTag>,
                        concurrent::snapshot_tag::Reader<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_authoring_witness<concurrent::snapshot_tag::Whole<UserTag>,
                                     concurrent::snapshot_tag::Writer<UserTag>,
                                     concurrent::snapshot_tag::Reader<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<concurrent::snapshot_tag::Whole<UserTag>,
                                          concurrent::snapshot_tag::Writer<UserTag>,
                                          concurrent::snapshot_tag::Reader<UserTag>> : std::true_type {};

}  // namespace crucible::safety
