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
// Old spelling: include/crucible/sessions/_SwmrSession.h.  Six
// deviations.  The caller mints the writer root and the reader root and
// names both brands in the session type, so no spelling here names the
// erased brand, and a writer permission of another brand does not make a
// second writer.  The handles bind their session through
// foundation::ChannelBinding, so a moved-from handle cannot publish or
// load.  The session mints take the handle by move, because
// fixy/session/Handle.h refuses a raw pointer to a handle as a session
// resource.  Only the plain protocol pair is carried: the pair over
// ContentAddressed<T> and Borrowed<T, Tag>, its two mints and
// load_borrowed_value had no consumer.  The mint_swmr_reader overload
// that took a SharedPermission ignored its proof, and is not carried.
// The read transport is a polling read over try_load, so the handle
// waits through the watch while a write is in flight, where the old
// transport spun inside load.

#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

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

namespace fixy::concurrent::swmr_session {

template <typename T>
using WriterRuntimeProto = ::fixy::session::Loop<::fixy::session::Send<T, ::fixy::session::Continue>>;

template <typename T>
using ReaderRuntimeProto = ::fixy::session::Loop<::fixy::session::Recv<T, ::fixy::session::Continue>>;

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
            std::declval<S&>().writer(std::declval<::foundation::permissions::Permission<
                                          typename S::writer_tag, typename S::writer_brand>&&>())
        } -> std::same_as<typename S::WriterHandle>;
        { std::declval<S&>().reader() } -> std::same_as<std::optional<typename S::ReaderHandle>>;
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
// mints two tokens of one brand, which no type can tell apart, so each
// root is minted once per program, as foundation/permissions/Permission.h
// says of every root.  The writer brand is never the erased brand, because
// a token of every brand converts to that brand and would open the writer.
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

    class WriterHandle {
        // The move clears the binding, so a moved-from writer cannot
        // publish while the permission lives in the handle it moved to.
        ::foundation::ChannelBinding<SwmrSession> session_;
        [[no_unique_address]] ::foundation::permissions::Permission<writer_tag, writer_brand> perm_;

        constexpr WriterHandle(SwmrSession& session,
                               ::foundation::permissions::Permission<writer_tag, writer_brand>&& perm) noexcept
            : session_{session}, perm_{std::move(perm)} {}

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
        constexpr WriterHandle& operator=(WriterHandle&&) noexcept = default;

        void publish(T const& value) noexcept { session_->snapshot_.publish(value); }

        [[nodiscard]] std::uint64_t version() const noexcept { return session_->snapshot_.version(); }
    };

    class ReaderHandle {
        // The move clears the binding, so a moved-from reader cannot load
        // while the share lives in the handle it moved to.
        ::foundation::ChannelBinding<SwmrSession> session_;
        ::foundation::permissions::SharedPermissionGuard<reader_tag, reader_brand> guard_;

        constexpr ReaderHandle(SwmrSession& session,
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

        [[nodiscard]] std::optional<T> try_load() const noexcept { return session_->snapshot_.try_load(); }

        [[nodiscard]] std::uint64_t version() const noexcept { return session_->snapshot_.version(); }

        [[nodiscard]] constexpr auto token() const noexcept
            -> ::foundation::permissions::SharedPermission<reader_tag, reader_brand> {
            return guard_.token();
        }
    };

    [[nodiscard]] constexpr WriterHandle
    writer(::foundation::permissions::Permission<writer_tag, writer_brand>&& perm) noexcept {
        return WriterHandle{*this, std::move(perm)};
    }

    [[nodiscard]] std::optional<ReaderHandle> reader() noexcept {
        auto guard = reader_pool_.lend();
        if (!guard) return std::nullopt;
        return ReaderHandle{*this, std::move(*guard)};
    }

    // Runs the body with every reader out, and returns false when readers
    // were still out and the body did not run.  The body gets no snapshot,
    // because the writer handle is still alive and publishes into the same
    // bytes: the body acts through the handles it holds.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = reader_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        reader_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    [[nodiscard]] std::uint64_t outstanding_readers() const noexcept { return reader_pool_.outstanding(); }

    [[nodiscard]] bool is_exclusive_active() const noexcept { return reader_pool_.is_exclusive_out(); }

    [[nodiscard]] std::uint64_t version() const noexcept { return snapshot_.version(); }

private:
    ::fixy::concurrent::AtomicSnapshot<T> snapshot_;
    ::foundation::permissions::SharedPermissionPool<reader_tag, reader_brand> reader_pool_;
};

template <SwmrSessionSurface Swmr>
[[nodiscard]] constexpr auto mint_swmr_writer(
    Swmr& session,
    ::foundation::permissions::Permission<typename Swmr::writer_tag, typename Swmr::writer_brand>&& perm) noexcept {
    return session.writer(std::move(perm));
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] auto mint_swmr_reader(Swmr& session) noexcept {
    return session.reader();
}

// The gate of each session mint: a channel of the right shape, the
// handle of the mint's role passed by move, and the gate of
// fixy::session::mint_session for the role's protocol over that handle.
// The handle is a template parameter so that each refusal is a clause
// of one concept: a type that is not a channel, a handle of the other
// role, and a handle passed as an lvalue all fail here.
template <typename Ctx, typename Swmr, typename Handle>
concept CtxFitsSwmrWriterSession =
    SwmrSessionSurface<Swmr> && SwmrWriterHandleOf<Swmr, Handle>
    && ::fixy::session::CtxFitsSession<Ctx, WriterRuntimeProto<typename Swmr::value_type>, Handle>;

template <typename Ctx, typename Swmr, typename Handle>
concept CtxFitsSwmrReaderSession =
    SwmrSessionSurface<Swmr> && std::same_as<Handle, typename Swmr::ReaderHandle>
    && ::fixy::session::CtxFitsSession<Ctx, ReaderRuntimeProto<typename Swmr::value_type>, Handle>;

template <typename Swmr, typename Ctx, typename Handle>
    requires CtxFitsSwmrWriterSession<Ctx, Swmr, Handle>
[[nodiscard]] constexpr auto mint_writer_runtime_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::mint_session<WriterRuntimeProto<typename Swmr::value_type>>(ctx,
                                                                                         std::forward<Handle>(handle));
}

template <typename Swmr, typename Ctx, typename Handle>
    requires CtxFitsSwmrReaderSession<Ctx, Swmr, Handle>
[[nodiscard]] constexpr auto mint_reader_runtime_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::mint_session<ReaderRuntimeProto<typename Swmr::value_type>>(ctx,
                                                                                         std::forward<Handle>(handle));
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

namespace detail::swmr_session_self_test {

struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct WriterBrand {};
struct OtherWriterBrand {};
struct ReaderBrand {};
using SmallSession = SwmrSession<int, WriterTag, ReaderTag, ReaderBrand, WriterBrand>;
using WriterHandle = SmallSession::WriterHandle;
using ReaderHandle = SmallSession::ReaderHandle;

static_assert(SwmrSessionSurface<SmallSession>);
static_assert(!SwmrSessionSurface<int>, "an int is not a channel");

// The session takes the writer permission of its own brand, and no other.
template <typename Session, typename Brand>
concept TakesWriterOfBrand = requires(Session& session) {
    session.writer(std::declval<::foundation::permissions::Permission<typename Session::writer_tag, Brand>&&>());
};
static_assert(TakesWriterOfBrand<SmallSession, WriterBrand>);
static_assert(!TakesWriterOfBrand<SmallSession, OtherWriterBrand>,
              "a writer permission of another brand would make a second writer");

// No session names the erased brand as its writer brand.
template <typename Brand>
concept NamesSessionOverWriterBrand = requires { typename SwmrSession<int, WriterTag, ReaderTag, ReaderBrand, Brand>; };
static_assert(NamesSessionOverWriterBrand<WriterBrand>);
static_assert(!NamesSessionOverWriterBrand<::foundation::brand::DefaultBrand>);

static_assert(sizeof(WriterHandle) == sizeof(SmallSession*),
              "SwmrSession::WriterHandle must EBO-collapse the writer Permission.");
static_assert(sizeof(ReaderHandle)
                  == sizeof(SmallSession*) + sizeof(::foundation::permissions::SharedPermissionGuard<ReaderTag, ReaderBrand>),
              "SwmrSession::ReaderHandle must only store a session pointer plus guard.");
static_assert(!std::is_copy_constructible_v<WriterHandle>);
static_assert(!std::is_copy_constructible_v<ReaderHandle>);
static_assert(std::is_move_constructible_v<WriterHandle>);
static_assert(std::is_move_constructible_v<ReaderHandle>);
static_assert(SwmrWriterHandleOf<SmallSession, WriterHandle>);
static_assert(!SwmrWriterHandleOf<SmallSession, WriterHandle&>, "a reference to a writer handle is not a handle");
static_assert(!SwmrWriterHandleOf<SmallSession, ReaderHandle>, "a reader handle is not a writer handle");

static_assert(std::is_same_v<WriterRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Send<int, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Recv<int, ::fixy::session::Continue>>>);

}  // namespace detail::swmr_session_self_test

}  // namespace fixy::concurrent::swmr_session
