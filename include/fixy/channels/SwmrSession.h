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
// load.  The read of the pole table is a polling read over try_load, so
// the handle waits through the watch while a write is in flight.
//
// The writer handle is linear (foundation/Linear.h).  Its door is
// release(), which gives the writer role back to the session and the
// token back to the caller.
//
// Frame doors
//     The frame doors open the end of one role and give the body a handle of
//     that end by value, as the frame doors of fixy/concurrent/
//     PermissionedChannel.h do: with_writer(session, token, body) gives the
//     result of the body, and with_reader(ctx, session, body) gives an Option
//     of the result, with no value while the reader root is out of the pool.
//     The handle type takes the scope brand of the call, so a handle does not
//     outlive the frame of its door.  The ends (WriterEnd and ReaderEnd) are
//     nested classes, so the check of synchronized{} of foundation/Transfer.h
//     reads their doors.  The type of an end stays spellable, but no code
//     outside the frame doors gets a value of it: each claim door takes a
//     SwmrFrameKey, whose only friends are the frame doors.  The residuals
//     of that header hold here too: a session in a Box or an Option that the
//     body ends, and an owner thread that keeps the closure past the body.

#include <fixy/Core.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
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

// The handles of the two roles (frame doors).  The definitions follow the
// session.
template <class Swmr, ::foundation::brand::IsBrand Scope>
class WriterHandle;
template <class Swmr, ::foundation::brand::IsBrand Scope>
class ReaderHandle;

// The key of the claim doors of a session (frame doors).  The definition
// follows the declarations of the doors that it befriends.
class SwmrFrameKey;

namespace detail {

// The handle that a frame door of the session gives its body on the scope
// brand Scope, through the member alias templates of the session.
template <class Swmr, class Scope>
using writer_handle_of_t = typename Swmr::template writer_handle<Scope>;
template <class Swmr, class Scope>
using reader_handle_of_t = typename Swmr::template reader_handle<Scope>;

}  // namespace detail

// The reader door lends a share of a pool, and the schedule decides its
// outcome, so the door takes a context that holds Nondet.  The surface reads
// the door with the witness context of the load of a reader pole
// (fixy/concurrent/HandleTraits.h), which holds Nondet.  Each claim door
// takes the key of the frame doors, which the surface names as a parameter
// and does not build.
template <typename S>
concept SwmrSessionSurface =
    requires(S& session, SwmrFrameKey const& key,
             ::foundation::permissions::Permission<typename S::writer_tag, typename S::writer_brand>&& token,
             ::fixy::concurrent::detail::load_witness_ctx const& ctx) {
        typename S::value_type;
        typename S::reader_tag;
        typename S::reader_brand;
        typename S::WriterEnd;
        typename S::ReaderEnd;
        { session.writer(key, std::move(token)) } -> std::same_as<typename S::WriterEnd>;
        { session.reader(key, ctx) } -> std::same_as<::fixy::Option<typename S::ReaderEnd>>;
    }
    && ::fixy::concurrent::IsSwmrWriter<typename S::WriterEnd>
    && ::fixy::concurrent::IsSwmrReader<typename S::ReaderEnd>
    && std::is_same_v<::fixy::concurrent::swmr_writer_value_t<typename S::WriterEnd>, typename S::value_type>
    && std::is_same_v<::fixy::concurrent::swmr_reader_value_t<typename S::ReaderEnd>, typename S::value_type>;

// The writer handle of a frame of the session, and the reader handle.  A
// reference to a handle is not a handle, so an lvalue fails.
template <typename Swmr, typename Handle>
concept SwmrWriterHandleOf = std::same_as<Handle, WriterHandle<Swmr, typename Handle::scope_type>>;
template <typename Swmr, typename Handle>
concept SwmrReaderHandleOf = std::same_as<Handle, ReaderHandle<Swmr, typename Handle::scope_type>>;

// The writer door (frame doors).  The writer token opens the end, and the
// body gets its handle.
template <class... Refused, class Swmr, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && SwmrSessionSurface<Swmr>
         && ::fixy::concurrent::HandleFrameLeaves<Body, Scope, detail::writer_handle_of_t<Swmr, Scope>>
auto with_writer(Swmr& session,
                 ::foundation::permissions::Permission<typename Swmr::writer_tag, typename Swmr::writer_brand>&& token,
                 Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::writer_handle_of_t<Swmr, Scope>>;

// The reader door (frame doors).  It lends a share of the reader pool, and it
// gives no value while the reader root is out of the pool.  Then a body that
// holds a linear value and no borrow comes back in the error of the result,
// and each other body ends in the door: a borrowing body ends each linear
// value through its own drop door.  That end is sound, because no value that
// owes a consume can end there (LendDoorBody of fixy/concurrent/HandleTraits.h).
// The context fits the lend of the pool (PoolCtx), so it holds Nondet.
template <class... Refused, class Ctx, class Swmr, class Body,
          ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0)
         && SwmrSessionSurface<Swmr> && ::foundation::permissions::PoolCtx<typename Swmr::reader_tag, Ctx>
         && ::fixy::concurrent::HandleFrameLeaves<Body, Scope, detail::reader_handle_of_t<Swmr, Scope>>
         && ::fixy::concurrent::LendDoorBody<Body>
[[nodiscard]] auto with_reader(Ctx const& ctx, Swmr& session, Body&& body) noexcept
    -> ::fixy::concurrent::detail::lend_result_t<Body,
                                                 std::invoke_result_t<Body&&, detail::reader_handle_of_t<Swmr, Scope>>>;

// A body that the caller names binds to these overloads, so the refusal
// gives its reason.
template <class... Refused, class Swmr, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && SwmrSessionSurface<Swmr>
             && ::fixy::concurrent::HandleNamedFrameBody<Body, detail::writer_handle_of_t<Swmr, Scope>>
void with_writer(Swmr& session,
                 ::foundation::permissions::Permission<typename Swmr::writer_tag, typename Swmr::writer_brand>&& token,
                 Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

template <class... Refused, class Ctx, class Swmr, class Body,
          ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0)
             && SwmrSessionSurface<Swmr> && ::foundation::permissions::PoolCtx<typename Swmr::reader_tag, Ctx>
             && ::fixy::concurrent::HandleNamedFrameBody<Body, detail::reader_handle_of_t<Swmr, Scope>>
void with_reader(Ctx const& ctx, Swmr& session, Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

// The key of the claim doors of a session (frame doors), as ChannelFrameKey of
// fixy/concurrent/PermissionedChannel.h is for a channel.  Its constructor is
// private and user-provided, its only friends are the frame doors, and it
// does not copy.  Each handle names the key as its one friend, and only
// build_handle_, a private door of the key, calls the constructor of a
// handle.
class SwmrFrameKey final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr SwmrFrameKey() noexcept {}

    // The handle of a frame over the end that a claim door gave.
    template <class Handle, class End>
    [[nodiscard]] static constexpr Handle build_handle_(End&& end) noexcept {
        return Handle{static_cast<End&&>(end)};
    }

    template <class... Refused, class Swmr, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0) && SwmrSessionSurface<Swmr>
             && ::fixy::concurrent::HandleFrameLeaves<Body, Scope, detail::writer_handle_of_t<Swmr, Scope>>
    friend auto
    with_writer(Swmr& session,
                ::foundation::permissions::Permission<typename Swmr::writer_tag, typename Swmr::writer_brand>&& token,
                Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::writer_handle_of_t<Swmr, Scope>>;

    template <class... Refused, class Ctx, class Swmr, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0)
             && SwmrSessionSurface<Swmr> && ::foundation::permissions::PoolCtx<typename Swmr::reader_tag, Ctx>
             && ::fixy::concurrent::HandleFrameLeaves<Body, Scope, detail::reader_handle_of_t<Swmr, Scope>>
             && ::fixy::concurrent::LendDoorBody<Body>
    friend auto with_reader(Ctx const& ctx, Swmr& session, Body&& body) noexcept
        -> ::fixy::concurrent::detail::lend_result_t<
            Body, std::invoke_result_t<Body&&, detail::reader_handle_of_t<Swmr, Scope>>>;

public:
    SwmrFrameKey(SwmrFrameKey const&) = delete("a key of the frame doors lives in the frame of one door call");
    SwmrFrameKey& operator=(SwmrFrameKey const&) = delete("a key of the frame doors does not change");
    ~SwmrFrameKey() = default;
};

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
//
// A reader on another thread loads a value that the writer published, maybe
// after the frame of the writer ended, so a value holds no borrow: it is
// SendableOwned (foundation/Transfer.h).
//
// The writer handle publishes and the reader handles load on their threads at
// the same time by design.  The snapshot cell orders each publish and each
// load, the pool counts the shares of the readers, and the claim keeps one
// writer.  Each member is private, and only the handles reach them, so the
// session states synchronized{} (foundation/Transfer.h).  No door keeps a
// borrow that a caller gives it, so it states synchronized_by_proof{} too
// (foundation/core/TypeParts.h), and a body that captures the session by
// reference passes the store check.
template <::fixy::concurrent::SnapshotValue T, typename WriterTag, typename ReaderTag,
          ::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsFreshBrand WriterBrand>
    requires ::foundation::transfer::SendableOwned<T>
class[[= ::foundation::transfer::synchronized{}]][[= ::foundation::core::synchronized_by_proof{}]] SwmrSession
    : public ::foundation::Pinned<SwmrSession<T, WriterTag, ReaderTag, ReaderBrand, WriterBrand>> {
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

    class[[= ::foundation::linear::linear{}]] WriterEnd {
        // The move clears the binding, so a moved-from writer cannot
        // publish while the permission lives in the handle it moved to.
        ::foundation::ChannelBinding<SwmrSession> session_;
        [[no_unique_address]] ::foundation::permissions::Permission<writer_tag, writer_brand> perm_;

        constexpr WriterEnd(SwmrSession& session,
                            ::foundation::permissions::Permission<writer_tag, writer_brand>&& perm) noexcept
            : session_{session, ::foundation::BindingKey<SwmrSession>{}}, perm_{std::move(perm)} {}

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

        WriterEnd(WriterEnd const&) = delete("SwmrSession::WriterEnd owns the linear writer permission");
        WriterEnd& operator=(WriterEnd const&) = delete("SwmrSession::WriterEnd owns the linear writer permission");
        constexpr WriterEnd(WriterEnd&&) noexcept = default;
        // An assignment would write over the writer token of the target, and
        // the move assignment of a Permission does nothing, so the token of
        // the target would leave through no door.
        WriterEnd& operator=(WriterEnd&&) = delete(
            "SwmrSession::WriterEnd owns the linear writer permission, and an assignment would drop the "
            "token of the target: bind the handle to a new name");
        ~WriterEnd() { release_claim_(); }

        // The release door.  The handle gives the writer role back to the
        // session and the token back to the caller, and it is unbound after
        // the call.  The token can open a writer handle again.
        [[nodiscard]] ::foundation::permissions::Permission<writer_tag, writer_brand> release() && noexcept {
            session_->writer_claim_.release();
            session_.unbind();
            return std::move(perm_);
        }

        // The writer end is the one writer of the snapshot, so it makes the
        // proof of that writer for each publish.
        void publish(T const& value) noexcept {
            session_->snapshot_.publish(::foundation::SoleWriterProof<WriterEnd, publish_role_>{}, value);
        }

        [[nodiscard]] std::uint64_t version() const noexcept { return session_->snapshot_.version(); }
    };

    class ReaderEnd {
        // The move clears the binding, so a moved-from reader cannot load
        // while the share lives in the handle it moved to.  The binding is to
        // a const session, so a reader reads the snapshot and writes no part
        // of the session: the guard gives its share back to the pool through
        // the count of the pool, which two threads can write.
        ::foundation::ChannelBinding<SwmrSession const> session_;
        ::foundation::permissions::SharedPermissionGuard<reader_tag, reader_brand> guard_;

        constexpr ReaderEnd(SwmrSession const& session,
                            ::foundation::permissions::SharedPermissionGuard<reader_tag, reader_brand>&& guard) noexcept
            : session_{session, ::foundation::BindingKey<SwmrSession const>{}}, guard_{std::move(guard)} {}

        friend class SwmrSession;

    public:
        using value_type = T;
        using tag_type = reader_tag;
        using row_discipline = ::fixy::row_discipline::swmr_reader;
        using row_payload = T;
        using channel_type = SwmrSession;

        ReaderEnd(ReaderEnd const&) = delete("SwmrSession::ReaderEnd owns one SharedPermissionPool share");
        ReaderEnd& operator=(ReaderEnd const&) = delete("SwmrSession::ReaderEnd owns one SharedPermissionPool share");
        constexpr ReaderEnd(ReaderEnd&&) noexcept = default;
        ReaderEnd& operator=(ReaderEnd&&) = delete("SwmrSession::ReaderEnd share lifetime is fixed at construction");

        // The load spins with no exit while a write is in flight, so the
        // context owns Div (AtomicSnapshot::load).  The value of a load is the
        // last publish before it, and the schedule of the writer decides which
        // publish that is.  So the context also owns Nondet.
        template <class Ctx>
            requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
                  && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>
        [[nodiscard]] T load(Ctx const& ctx) const noexcept {
            return session_->snapshot_.load(ctx);
        }

        // The value, or no value while a write is in flight.  It does not
        // wait.  The schedule decides the value and the outcome, so the call
        // takes the tag of Nondet, as empty_approx of a channel does.
        [[nodiscard]] ::fixy::Option<T> try_load(::foundation::effects::cap::Nondet) const noexcept {
            return session_->snapshot_.try_load();
        }

        [[nodiscard]] std::uint64_t version() const noexcept { return session_->snapshot_.version(); }

        [[nodiscard]] constexpr auto token() const noexcept
            -> ::foundation::permissions::SharedPermission<reader_tag, reader_brand> {
            return guard_.token();
        }
    };

    // The handles that the frame doors give a body on the scope brand Scope.
    template <::foundation::brand::IsBrand Scope>
    using writer_handle = WriterHandle<SwmrSession, Scope>;
    template <::foundation::brand::IsBrand Scope>
    using reader_handle = ReaderHandle<SwmrSession, Scope>;

    // The claim doors of the ends.  Each takes the key of the frame doors, so
    // only a frame door opens an end, and the end lives in a handle of the
    // frame.

    // Takes the claim of the writer role first, so a second live writer
    // handle ends the process here.
    [[nodiscard]] WriterEnd writer(SwmrFrameKey const& /*key*/,
                                   ::foundation::permissions::Permission<writer_tag, writer_brand>&& perm) noexcept {
        writer_claim_.take("two live writer handles of one SWMR session. One call site minted the writer root two "
                           "times.");
        return WriterEnd{*this, std::move(perm)};
    }

    // The door is out of line.  Inline, the test of the lend and the end of
    // the Option of the handle in a caller give a false maybe-uninitialized
    // error of GCC 16 at -O1 with TSan, because the compiler does not see that
    // the Option holds no handle on the path where the pool lent no share.
    //
    // A claim door (the doors of a synchronized class of foundation/
    // Transfer.h): one compare-exchange of the pool makes the share, and the
    // handle holds it.  After the compare-exchange the door writes only the
    // handle, and a reader handle reads the snapshot with atomic steps.  The
    // lend fails while the reader root is out, and another thread deposits
    // the root at a time that the schedule decides, so the door takes a
    // context that holds Nondet (PoolCtx).
    template <class Ctx>
        requires ::foundation::permissions::PoolCtx<reader_tag, Ctx>
    [[nodiscard, gnu::noinline]] ::fixy::Option<ReaderEnd> reader(SwmrFrameKey const& /*key*/,
                                                                  Ctx const& ctx) noexcept {
        CRUCIBLE_TRY(lent, reader_pool_.lend(ctx));
        return ::fixy::some(ReaderEnd{*this, std::move(lent)});
    }

    // The reader root while it is out of the pool (the lease of
    // foundation/permissions/SharedPermission.h).
    using reader_lease = ::foundation::permissions::ExclusiveLease<reader_tag, reader_brand>;

    // Takes the reader root out of the pool when no reader handle lives, or
    // gives no value while one lives.  The root comes out as the lease of the
    // pool of this session.  While the root is out, the pool lends no share,
    // so no reader loads: the writer can reset the value with no read at the
    // same time.  deposit_reader_root gives the lease back, and the pool lends
    // again.  These are the doors of the producer root of a merge channel of
    // fixy/concurrent/PermissionedChannel.h, for the pooled side of this
    // session.
    //
    // A claim door: its result is the exclusive token of the pool, which one
    // compare-exchange of the pool makes.  The write of the parked root after
    // that compare-exchange happens only on the thread that won it, until that
    // thread deposits the lease.  A reader handle ends at a time that the
    // schedule decides, so the door takes a context that holds Nondet.
    template <class Ctx>
        requires ::foundation::permissions::PoolCtx<reader_tag, Ctx>
    [[nodiscard]] ::fixy::Option<reader_lease> try_withdraw_reader_root(Ctx const& ctx) noexcept {
        return reader_pool_.try_upgrade(ctx);
    }

    // Gives the lease of the reader root back to the pool.  A lease of another
    // pool, and a deposit while the root is in the pool, end the process
    // (SharedPermissionPool::deposit_exclusive).
    void deposit_reader_root(reader_lease&& root) noexcept { reader_pool_.deposit_exclusive(std::move(root)); }

    [[nodiscard]] std::uint64_t outstanding_readers() const noexcept { return reader_pool_.outstanding(); }

    [[nodiscard]] bool is_exclusive_active() const noexcept { return reader_pool_.is_exclusive_out(); }

    [[nodiscard]] std::uint64_t version() const noexcept { return snapshot_.version(); }

private:
    // The role of the publish of the writer end in the proof of the one
    // writer of the snapshot.
    struct publish_role_ final {};

    // The writer end is the one writer of the snapshot: one writer token of
    // the brand opens it, and the claim of the writer role refuses a second
    // live end of one token site at its mint.  So the snapshot names the
    // writer end as the owner of its one writer, and a publish takes no
    // locked step.
    ::fixy::concurrent::AtomicSnapshot<T, ::foundation::SoleWriterProof<WriterEnd, publish_role_>> snapshot_;
    ::foundation::permissions::SharedPermissionPool<reader_tag, reader_brand> reader_pool_;
    ::foundation::EndpointClaim writer_claim_;
};

// The writer handle of a frame (frame doors): the writer end of a session on
// the scope brand of one call of a frame door.  The handle derives from its
// end privately, so no code outside it binds a reference to the end, and it
// shows each door of the end.  The constructor is private, and the key of the
// frame doors is its one friend, so only a frame door builds a handle.  The
// writer end is linear, so the handle is linear too.  Its first argument is
// the session, which is no linear value, so the handle marks its own end
// (foundation/Linear.h).
template <class Swmr, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear{}]] WriterHandle : private Swmr::WriterEnd {
    using End = typename Swmr::WriterEnd;

    explicit WriterHandle(End&& end) noexcept : End{std::move(end)} {}
    friend class SwmrFrameKey;

public:
    using value_type = typename End::value_type;
    using tag_type = typename End::tag_type;
    using brand_type = typename End::brand_type;
    using row_discipline = typename End::row_discipline;
    using row_payload = typename End::row_payload;
    using channel_type = typename End::channel_type;
    // The scope brand of the frame of the handle.
    using scope_type = Scope;

    WriterHandle(WriterHandle&&) noexcept = default;
    WriterHandle(WriterHandle const&) = delete("a writer handle owns the linear writer permission");
    WriterHandle& operator=(WriterHandle const&) = delete("a writer handle owns the linear writer permission");
    WriterHandle& operator=(WriterHandle&&) = delete(
        "a writer handle owns the linear writer permission, and an assignment would drop the token of the "
        "target");
    ~WriterHandle() = default;

    // The end whose doors the handle shows.  The shape traits of fixy/
    // concurrent/HandleTraits.h read the signature of each door there.
    using frame_end_type = End;

    // The doors of the end.  Each door forwards to the door of the same name
    // of the end, which gives a value and no reference or address into the
    // session.  The handle states each door as its own member, so the rule
    // escape-doors reads it.
    void publish(value_type const& value) noexcept { End::publish(value); }

    [[nodiscard]] auto release() && noexcept { return static_cast<End&&>(*this).release(); }

    [[nodiscard]] std::uint64_t version() const noexcept { return End::version(); }
};

// The reader handle of a frame (frame doors): the reader end of a session on
// the scope brand of one call of a frame door, as WriterHandle is for the
// writer.
template <class Swmr, ::foundation::brand::IsBrand Scope>
class ReaderHandle : private Swmr::ReaderEnd {
    using End = typename Swmr::ReaderEnd;

    explicit ReaderHandle(End&& end) noexcept : End{std::move(end)} {}
    friend class SwmrFrameKey;

public:
    using value_type = typename End::value_type;
    using tag_type = typename End::tag_type;
    using row_discipline = typename End::row_discipline;
    using row_payload = typename End::row_payload;
    using channel_type = typename End::channel_type;
    // The scope brand of the frame of the handle.
    using scope_type = Scope;

    ReaderHandle(ReaderHandle&&) noexcept = default;
    ReaderHandle(ReaderHandle const&) = delete("a reader handle owns one share of the reader pool");
    ReaderHandle& operator=(ReaderHandle const&) = delete("a reader handle owns one share of the reader pool");
    ReaderHandle& operator=(ReaderHandle&&) = delete("the life of the share of a reader handle is fixed at its build");
    ~ReaderHandle() = default;

    // The end whose doors the handle shows, as for the writer handle.
    using frame_end_type = End;

    // The doors of the end, as the writer handle states them.  The load keeps
    // the gate of the load of the end.
    template <class Ctx>
        requires requires(End const& end, Ctx const& ctx) { end.load(ctx); }
    [[nodiscard]] value_type load(Ctx const& ctx) const noexcept {
        return End::load(ctx);
    }

    [[nodiscard]] auto token() const noexcept { return End::token(); }

    [[nodiscard]] ::fixy::Option<value_type> try_load(::foundation::effects::cap::Nondet nondet) const noexcept {
        return End::try_load(nondet);
    }

    [[nodiscard]] std::uint64_t version() const noexcept { return End::version(); }
};

template <class... Refused, class Swmr, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0) && SwmrSessionSurface<Swmr>
         && ::fixy::concurrent::HandleFrameLeaves<Body, Scope, detail::writer_handle_of_t<Swmr, Scope>>
auto with_writer(Swmr& session,
                 ::foundation::permissions::Permission<typename Swmr::writer_tag, typename Swmr::writer_brand>&& token,
                 Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::writer_handle_of_t<Swmr, Scope>> {
    SwmrFrameKey const key{};
    return ::foundation::linear::call_once(
        static_cast<Body&&>(body),
        SwmrFrameKey::build_handle_<detail::writer_handle_of_t<Swmr, Scope>>(session.writer(key, std::move(token))));
}

template <class... Refused, class Ctx, class Swmr, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0)
         && SwmrSessionSurface<Swmr> && ::foundation::permissions::PoolCtx<typename Swmr::reader_tag, Ctx>
         && ::fixy::concurrent::HandleFrameLeaves<Body, Scope, detail::reader_handle_of_t<Swmr, Scope>>
         && ::fixy::concurrent::LendDoorBody<Body>
auto with_reader(Ctx const& ctx, Swmr& session, Body&& body) noexcept -> ::fixy::concurrent::detail::lend_result_t<
    Body, std::invoke_result_t<Body&&, detail::reader_handle_of_t<Swmr, Scope>>> {
    using handle_type = detail::reader_handle_of_t<Swmr, Scope>;
    using result_type = std::invoke_result_t<Body&&, handle_type>;
    SwmrFrameKey const key{};
    CRUCIBLE_LET_ELSE(lent, session.reader(key, ctx),
                      ::fixy::concurrent::detail::refuse_lend_<Body, result_type>(static_cast<Body&&>(body)));
    handle_type handle = SwmrFrameKey::build_handle_<handle_type>(std::move(lent));
    if constexpr (std::is_void_v<result_type>) {
        ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(handle));
        return ::fixy::concurrent::detail::lent_value_<Body, result_type>(::foundation::core::Unit{});
    } else {
        return ::fixy::concurrent::detail::lent_value_<Body, result_type>(
            ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(handle)));
    }
}

// A writer handle and a reader handle of a frame of the session that the
// handle names.
template <class Pole>
concept SwmrWriterPole = std::same_as<Pole, WriterHandle<typename Pole::channel_type, typename Pole::scope_type>>;
template <class Pole>
concept SwmrReaderPole = std::same_as<Pole, ReaderHandle<typename Pole::channel_type, typename Pole::scope_type>>;

// The pole table of a session (fixy/session/Admission.h, section The pole
// table of a channel): the writer handle runs the writer protocol, and each
// reader handle runs the reader protocol.  A publish never lacks room, so
// the write always takes the value.  The read gives the snapshot, or none
// while a write is in flight.  The write does not read the context of the
// step.  The schedule decides the value of a read, so the read takes the
// bare tag of Nondet from the context of the step, and that context holds
// Nondet.
template <class Pole>
    requires SwmrSessionSurface<typename Pole::channel_type> && (SwmrWriterPole<Pole> || SwmrReaderPole<Pole>)
struct swmr_pole_protocols {
    using value_type = typename Pole::channel_type::value_type;
    static constexpr bool is_writer = SwmrWriterPole<Pole>;
    using writer_protocol = WriterRuntimeProto<value_type>;
    using reader_protocol = ReaderRuntimeProto<value_type>;
    using self_protocol = std::conditional_t<is_writer, writer_protocol, reader_protocol>;
    using peer_protocol = std::conditional_t<is_writer, reader_protocol, writer_protocol>;

    template <::foundation::effects::IsExecCtx Ctx>
        requires is_writer
    [[nodiscard]] static constexpr bool write(Ctx const& /*ctx*/, Pole& pole, value_type& value) noexcept {
        pole.publish(value);
        return true;
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires(!is_writer) && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>
    [[nodiscard]] static constexpr ::fixy::Option<value_type> read(Ctx const& ctx, Pole& pole) noexcept {
        return pole.try_load(::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx));
    }
};

// The gate of each session mint: a channel of the right shape, the
// handle of the mint's role passed by move, and the gate of the pole door
// of fixy/session/Handle.h for that handle.  The handle is a template
// parameter so that each refusal is a clause of one concept: a type that
// is not a channel, a handle of the other role, and a handle passed as an
// lvalue all fail here.  The read of a reader pole takes the bare tag of
// Nondet from the context of the step, so the context of a reader session
// holds Nondet, and a context with no Nondet fails at the mint.
template <typename Ctx, typename Swmr, typename Handle>
concept CtxFitsSwmrWriterSession =
    SwmrSessionSurface<Swmr> && SwmrWriterHandleOf<Swmr, Handle> && ::fixy::session::CtxFitsPoleSession<Ctx, Handle>;

template <typename Ctx, typename Swmr, typename Handle>
concept CtxFitsSwmrReaderSession =
    SwmrSessionSurface<Swmr> && SwmrReaderHandleOf<Swmr, Handle> && ::fixy::session::CtxFitsPoleSession<Ctx, Handle>
    && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>;

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

// The tags and the brands that the witness rows of the handles of a frame
// name.  No code builds a session of them.  They are outside a detail
// namespace, so the witness fixture of each row uses no detail name.
namespace frame_witness {
struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct WriterBrand {};
struct ReaderBrand {};
}  // namespace frame_witness

}  // namespace fixy::channels::swmr_session

// The family of this header in the root of the pole tables.  The
// registration stands in the file of its template.
namespace fixy::session::pole_tables::swmr {

inline constexpr ::fixy::session::pole_table swmr_session{.table =
                                                              ^^::fixy::channels::swmr_session::swmr_pole_protocols};

inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};

}  // namespace fixy::session::pole_tables::swmr
