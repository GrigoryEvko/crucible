#pragma once

// A single-writer many-reader channel over AtomicSnapshot.  One writer
// handle holds the linear writer permission and publishes.  Reader
// handles each hold one share of a pool, and a drained pool gives
// exclusive access for a reset.  A later publish overwrites an earlier
// one, so a reader sees the latest value and no history.
//
// Each side can run as a local session over its own handle, with the
// empty permission set.  The session mints take the handle by move, so
// no session holds a pointer to a handle that can move or die first,
// and the caller never acts on the channel beside the session.  Each
// protocol loops with no exit branch, and the session ends with a typed
// detach.
//
// The caller mints the writer root and the reader root and names both
// brands in the session type, so no spelling here names the erased
// brand, and a writer permission of another brand does not make a
// second writer.  A claim on the writer role refuses a second live
// writer of one brand.  The handles bind their session through
// foundation::ChannelBinding, so a moved-from handle cannot publish or
// load.  The read transport is a polling read over try_load, so the
// handle waits through the watch while a write is in flight.
//
// The writer handle is linear (foundation/Linear.h).  Its door is
// release(), which gives the writer role back to the session and the
// token back to the caller.

#include <fixy/Core.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct swmr_session;
struct swmr_writer;
struct swmr_reader;
}  // namespace fixy::row_discipline

namespace fixy::channels::swmr_session {

template <typename T>
using WriterRuntimeProto = ::fixy::session::Loop<::fixy::session::Send<T, ::fixy::session::Continue>>;

// The protocol of a reader is the dual of the protocol of the writer, so the
// two sides of the session cannot drift apart.
template <typename T>
using ReaderRuntimeProto = ::fixy::session::dual_of_t<WriterRuntimeProto<T>>;

template <typename S>
concept SwmrSessionSurface =
    requires {
        typename S::value_type;
        typename S::writer_tag;
        typename S::writer_brand;
        typename S::reader_tag;
        typename S::reader_brand;
        typename S::WriterHandle;
        typename S::ReaderHandle;
        {
            std::declval<S&>().writer(
                std::declval<
                    ::foundation::permissions::Permission<typename S::writer_tag, typename S::writer_brand>&&>())
        } -> std::same_as<typename S::WriterHandle>;
        { std::declval<S&>().reader() } -> std::same_as<::fixy::Option<typename S::ReaderHandle>>;
    } && ::fixy::concurrent::IsSwmrWriter<typename S::WriterHandle>
    && ::fixy::concurrent::IsSwmrReader<typename S::ReaderHandle>
    && std::is_same_v<::fixy::concurrent::swmr_writer_value_t<typename S::WriterHandle>, typename S::value_type>
    && std::is_same_v<::fixy::concurrent::swmr_reader_value_t<typename S::ReaderHandle>, typename S::value_type>;

// The writer handle of the channel.  A reference to a handle is not a
// handle, so an lvalue fails.
template <typename Swmr, typename Handle>
concept SwmrWriterHandleOf = std::same_as<Handle, typename Swmr::WriterHandle>;

// The caller mints the reader root and hands it to the constructor, and
// the pool that holds it is the root of trust for the reader tag.  The
// caller mints and keeps the writer permission.  The brand of each root
// is a parameter of the session type, because a member cannot carry a
// brand that a header mints.  The writer brand is what keeps one writer:
// a root minted at another site has another brand, and writer() refuses
// it, so a second publisher does not compile.  One site that runs twice
// mints two tokens of one brand, which no type can tell apart.  The run-
// time floor there is the claim of the writer role: writer() takes it,
// and a second live writer handle ends the process at its mint, not at a
// concurrent publish.  The writer handle gives the claim back when it
// ends.  The writer brand is never the erased brand, because a token of
// every brand converts to that brand and would open the writer.
template <::fixy::concurrent::SnapshotValue T, typename WriterTag, typename ReaderTag,
          ::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsFreshBrand WriterBrand>
class SwmrSession : public ::foundation::Pinned<SwmrSession<T, WriterTag, ReaderTag, ReaderBrand, WriterBrand>> {
public:
    using value_type = T;
    using writer_tag = WriterTag;
    using writer_brand = WriterBrand;
    using reader_tag = ReaderTag;
    using reader_brand = ReaderBrand;
    using row_discipline = ::fixy::row_discipline::swmr_session;
    using row_payload = T;

    explicit SwmrSession(::foundation::permissions::Permission<reader_tag, reader_brand>&& reader_root) noexcept
        : snapshot_{}, reader_pool_{std::move(reader_root)} {}

    SwmrSession(::foundation::permissions::Permission<reader_tag, reader_brand>&& reader_root,
                T const& initial) noexcept
        : snapshot_{initial}, reader_pool_{std::move(reader_root)} {}

    class[[= ::foundation::linear::linear{}]] WriterHandle {
        // The move clears the binding, so a moved-from writer cannot
        // publish while the permission lives in the handle it moved to.
        ::foundation::ChannelBinding<SwmrSession> session_;
        [[no_unique_address]] ::foundation::permissions::Permission<writer_tag, writer_brand> perm_;

        constexpr WriterHandle(SwmrSession& session,
                               ::foundation::permissions::Permission<writer_tag, writer_brand>&& perm) noexcept
            : session_{session}, perm_{std::move(perm)} {}

        // A moved-from handle holds no claim, so only the bound one gives
        // the role back.
        void release_claim_() noexcept {
            if (session_.is_bound()) session_->writer_claim_.release();
        }

        friend class SwmrSession;

    public:
        using value_type = T;
        using tag_type = writer_tag;
        using brand_type = writer_brand;
        using row_discipline = ::fixy::row_discipline::swmr_writer;
        using row_payload = T;
        // The cell this handle acts on.  No stage drains a cell, so a
        // pipeline joins no stage after the writer.
        using channel_type = SwmrSession;

        WriterHandle(WriterHandle const&) = delete("SwmrSession::WriterHandle owns the linear writer permission");
        WriterHandle&
        operator=(WriterHandle const&) = delete("SwmrSession::WriterHandle owns the linear writer permission");
        constexpr WriterHandle(WriterHandle&&) noexcept = default;
        // An assignment would write over the writer token of the target, and
        // the move assignment of a Permission does nothing, so the token of
        // the target would leave through no door.
        WriterHandle& operator=(WriterHandle&&) = delete(
            "SwmrSession::WriterHandle owns the linear writer permission, and an assignment would drop the "
            "token of the target: bind the handle to a new name");
        ~WriterHandle() { release_claim_(); }

        // The release door.  The handle gives the writer role back to the
        // session and the token back to the caller, and it is unbound after
        // the call.  The token can open a writer handle again.
        [[nodiscard]] ::foundation::permissions::Permission<writer_tag, writer_brand> release() && noexcept {
            session_->writer_claim_.release();
            session_.unbind();
            return std::move(perm_);
        }

        void publish(T const& value) noexcept { session_->snapshot_.publish(value); }

        [[nodiscard]] std::uint64_t version() const noexcept { return session_->snapshot_.version(); }
    };

    class ReaderHandle {
        // The move clears the binding, so a moved-from reader cannot load
        // while the share lives in the handle it moved to.
        ::foundation::ChannelBinding<SwmrSession> session_;
        ::foundation::permissions::SharedPermissionGuard<reader_tag, reader_brand> guard_;

        constexpr ReaderHandle(
            SwmrSession& session,
            ::foundation::permissions::SharedPermissionGuard<reader_tag, reader_brand>&& guard) noexcept
            : session_{session}, guard_{std::move(guard)} {}

        friend class SwmrSession;

    public:
        using value_type = T;
        using tag_type = reader_tag;
        using row_discipline = ::fixy::row_discipline::swmr_reader;
        using row_payload = T;
        using channel_type = SwmrSession;

        ReaderHandle(ReaderHandle const&) = delete("SwmrSession::ReaderHandle owns one SharedPermissionPool share");
        ReaderHandle&
        operator=(ReaderHandle const&) = delete("SwmrSession::ReaderHandle owns one SharedPermissionPool share");
        constexpr ReaderHandle(ReaderHandle&&) noexcept = default;
        ReaderHandle&
        operator=(ReaderHandle&&) = delete("SwmrSession::ReaderHandle share lifetime is fixed at construction");

        [[nodiscard]] T load() const noexcept { return session_->snapshot_.load(); }

        [[nodiscard]] ::fixy::Option<T> try_load() const noexcept { return session_->snapshot_.try_load(); }

        [[nodiscard]] std::uint64_t version() const noexcept { return session_->snapshot_.version(); }

        [[nodiscard]] constexpr auto token() const noexcept
            -> ::foundation::permissions::SharedPermission<reader_tag, reader_brand> {
            return guard_.token();
        }
    };

    // Takes the claim of the writer role first, so a second live writer
    // handle ends the process here.
    [[nodiscard]] WriterHandle writer(::foundation::permissions::Permission<writer_tag, writer_brand>&& perm) noexcept {
        writer_claim_.take("two live writer handles of one SWMR session. One call site minted the writer root two "
                           "times.");
        return WriterHandle{*this, std::move(perm)};
    }

    // The door is out of line.  Inline, the test of the lend and the end of
    // the Option of the handle in a caller give a false maybe-uninitialized
    // error of GCC 16 at -O1 with TSan, because the compiler does not see that
    // the Option holds no handle on the path where the pool lent no share.
    [[nodiscard, gnu::noinline]] ::fixy::Option<ReaderHandle> reader() noexcept {
        auto lent = ::foundation::permissions::detail::ChannelPoolDoor::lend(reader_pool_);
        if (lent.is_none()) return ::fixy::none;
        return ::fixy::some(ReaderHandle{*this, std::move(lent).expect("SwmrSession::reader: the pool lent a share")});
    }

    // Takes the reader root out of the pool when no reader handle lives, or
    // gives no value while one lives.  While the root is out, the pool lends no
    // share, so no reader loads: the writer can reset the value with no read at
    // the same time.  deposit_reader_root gives the root back, and the pool
    // lends again.  These are the doors of the producer root of a merge channel
    // of fixy/concurrent/PermissionedChannel.h, for the pooled side of this
    // session.
    [[nodiscard]] ::fixy::Option<::foundation::permissions::Permission<reader_tag, reader_brand>>
    try_withdraw_reader_root() noexcept {
        return ::foundation::permissions::detail::ChannelPoolDoor::try_upgrade(reader_pool_);
    }

    // Gives the reader root back to the pool.  A deposit while the root is in
    // the pool ends the process (SharedPermissionPool::deposit_exclusive).
    void deposit_reader_root(::foundation::permissions::Permission<reader_tag, reader_brand>&& root) noexcept {
        reader_pool_.deposit_exclusive(std::move(root));
    }

    [[nodiscard]] std::uint64_t outstanding_readers() const noexcept { return reader_pool_.outstanding(); }

    [[nodiscard]] bool is_exclusive_active() const noexcept { return reader_pool_.is_exclusive_out(); }

    [[nodiscard]] std::uint64_t version() const noexcept { return snapshot_.version(); }

private:
    ::fixy::concurrent::AtomicSnapshot<T> snapshot_;
    ::foundation::permissions::SharedPermissionPool<reader_tag, reader_brand> reader_pool_;
    ::foundation::EndpointClaim writer_claim_;
};

template <SwmrSessionSurface Swmr>
[[nodiscard]] auto mint_swmr_writer(
    Swmr& session,
    ::foundation::permissions::Permission<typename Swmr::writer_tag, typename Swmr::writer_brand>&& perm) noexcept {
    return session.writer(std::move(perm));
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] auto mint_swmr_reader(Swmr& session) noexcept {
    return session.reader();
}

// The pole table of a session (fixy/session/Admission.h, section The pole
// table of a channel): the writer handle runs the writer protocol, and each
// reader handle runs the reader protocol.
template <class Pole>
    requires SwmrSessionSurface<typename Pole::channel_type>
          && (std::same_as<Pole, typename Pole::channel_type::WriterHandle>
              || std::same_as<Pole, typename Pole::channel_type::ReaderHandle>)
struct swmr_pole_protocols {
    static constexpr bool is_writer = std::same_as<Pole, typename Pole::channel_type::WriterHandle>;
    using writer_protocol = WriterRuntimeProto<typename Pole::channel_type::value_type>;
    using reader_protocol = ReaderRuntimeProto<typename Pole::channel_type::value_type>;
    using self_protocol = std::conditional_t<is_writer, writer_protocol, reader_protocol>;
    using peer_protocol = std::conditional_t<is_writer, reader_protocol, writer_protocol>;
};

// The gate of each session mint: a channel of the right shape, the
// handle of the mint's role passed by move, and the gate of the pole door
// of fixy/session/Handle.h for that handle.  The handle is a template
// parameter so that each refusal is a clause of one concept: a type that
// is not a channel, a handle of the other role, and a handle passed as an
// lvalue all fail here.
template <typename Ctx, typename Swmr, typename Handle>
concept CtxFitsSwmrWriterSession =
    SwmrSessionSurface<Swmr> && SwmrWriterHandleOf<Swmr, Handle> && ::fixy::session::CtxFitsPoleSession<Ctx, Handle>;

template <typename Ctx, typename Swmr, typename Handle>
concept CtxFitsSwmrReaderSession = SwmrSessionSurface<Swmr> && std::same_as<Handle, typename Swmr::ReaderHandle>
                                && ::fixy::session::CtxFitsPoleSession<Ctx, Handle>;

// Each mint opens the handle on the protocol that the table above names
// for its role.
template <typename Swmr, typename Ctx, typename Handle>
    requires CtxFitsSwmrWriterSession<Ctx, Swmr, Handle>
[[nodiscard]] constexpr auto mint_writer_runtime_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::SessionMintDoor::open_pole<::fixy::session::DefaultAbandonmentPolicy>(
        ctx, std::forward<Handle>(handle));
}

template <typename Swmr, typename Ctx, typename Handle>
    requires CtxFitsSwmrReaderSession<Ctx, Swmr, Handle>
[[nodiscard]] constexpr auto mint_reader_runtime_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::SessionMintDoor::open_pole<::fixy::session::DefaultAbandonmentPolicy>(
        ctx, std::forward<Handle>(handle));
}

// The write of the writer session: a publish never lacks room, so the
// trying write always takes the value.
inline constexpr auto publish_value = [](auto& writer, auto& value) noexcept -> bool {
    writer.publish(value);
    return true;
};

// The read of the reader session: the snapshot, or none while a write is
// in flight.  The handle then waits through the watch and reads again.
inline constexpr auto load_value = [](auto& reader) noexcept { return reader.try_load(); };

// The tags and the brands that only checks name.  The witness roster
// (the witness rows of utils/rule-table.txt), the permission-row roster of
// test/test_permission_row_roster.cpp and the check file of this header
// spell a session over them, and no code builds one.
namespace detail::swmr_session_witness {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct WriterBrand {};
struct ReaderBrand {};
}  // namespace detail::swmr_session_witness

}  // namespace fixy::channels::swmr_session

// The family of this header in the root of the pole tables.  The
// registration stands in the file of its template.
namespace fixy::session::pole_tables::swmr {

inline constexpr ::fixy::session::pole_table swmr_session{.table =
                                                              ^^::fixy::channels::swmr_session::swmr_pole_protocols};

inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};

}  // namespace fixy::session::pole_tables::swmr
