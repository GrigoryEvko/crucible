#pragma once

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::snapshot_session {

template <typename T>
using WriterProto = Loop<Send<T, Continue>>;

template <typename T>
using ReaderProto = Loop<Recv<T, Continue>>;

template <typename Snap>
concept SnapshotSessionSurface =
    requires(Snap& snap, typename Snap::WriterHandle& writer_handle, typename Snap::ReaderHandle& reader_handle,
             ::crucible::safety::Permission<typename Snap::writer_tag> writer_perm,
             typename Snap::value_type const& value) {
        typename Snap::value_type;
        typename Snap::writer_tag;
        typename Snap::reader_tag;
        typename Snap::WriterHandle;
        typename Snap::ReaderHandle;

        { snap.writer(std::move(writer_perm)) } -> std::same_as<typename Snap::WriterHandle>;
        { snap.reader() } -> std::same_as<std::optional<typename Snap::ReaderHandle>>;
        { writer_handle.publish(value) } noexcept;
        { reader_handle.load() } -> std::same_as<typename Snap::value_type>;
        { reader_handle.try_load() } -> std::same_as<std::optional<typename Snap::value_type>>;
    };

template <SnapshotSessionSurface Snap>
[[nodiscard]] constexpr auto
mint_snapshot_writer(Snap& snap, ::crucible::safety::Permission<typename Snap::writer_tag>&& perm) noexcept {
    return snap.writer(std::move(perm));
}

template <SnapshotSessionSurface Snap>
[[nodiscard]] auto mint_snapshot_reader(Snap& snap) noexcept {
    return snap.reader();
}

template <SnapshotSessionSurface Snap, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_snapshot_writer_session(Ctx const& ctx,
                                                          typename Snap::WriterHandle& handle) noexcept {
    using T = typename Snap::value_type;
    return mint_permissioned_session<WriterProto<T>>(ctx, &handle);
}

template <SnapshotSessionSurface Snap, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_snapshot_reader_session(Ctx const& ctx,
                                                          typename Snap::ReaderHandle& handle) noexcept {
    using T = typename Snap::value_type;
    return mint_permissioned_session<ReaderProto<T>>(ctx, &handle);
}

template <SnapshotSessionSurface Snap, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using WriterSessionHandle = decltype(mint_snapshot_writer_session<Snap>(std::declval<Ctx const&>(),
                                                                        std::declval<typename Snap::WriterHandle&>()));

template <SnapshotSessionSurface Snap, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ReaderSessionHandle = decltype(mint_snapshot_reader_session<Snap>(std::declval<Ctx const&>(),
                                                                        std::declval<typename Snap::ReaderHandle&>()));

inline constexpr auto blocking_publish = [](auto& hp, auto const& value) noexcept { hp->publish(value); };

inline constexpr auto blocking_load = [](auto& hp) noexcept { return hp->load(); };

inline constexpr auto blocking_try_load = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_load()) return *v;
        CRUCIBLE_SPIN_PAUSE;
    }
};

namespace detail::snapshot_session_self_test {

struct Tag {};
using Snap = ::crucible::concurrent::PermissionedSnapshot<int, Tag>;
using WriterHandle = Snap::WriterHandle;
using ReaderHandle = Snap::ReaderHandle;
using WriterSession = WriterSessionHandle<Snap>;
using ReaderSession = ReaderSessionHandle<Snap>;

static_assert(SnapshotSessionSurface<Snap>);
static_assert(std::is_same_v<WriterProto<int>, Loop<Send<int, Continue>>>);
static_assert(std::is_same_v<ReaderProto<int>, Loop<Recv<int, Continue>>>);
static_assert(std::is_same_v<typename WriterSession::protocol, Send<int, Continue>>);
static_assert(std::is_same_v<typename ReaderSession::protocol, Recv<int, Continue>>);
static_assert(std::is_same_v<typename WriterSession::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<typename ReaderSession::perm_set, EmptyPermSet>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, WriterHandle*>)
                  == sizeof(SessionHandle<End, WriterHandle*>),
              "snapshot_session: writer PSH pointer resource must remain same size "
              "as bare SessionHandle.");
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, ReaderHandle*>)
                  == sizeof(SessionHandle<End, ReaderHandle*>),
              "snapshot_session: reader PSH pointer resource must remain same size "
              "as bare SessionHandle.");
static_assert(
    sizeof(WriterSession)
        == sizeof(SessionHandle<typename WriterSession::protocol, WriterHandle*, typename WriterSession::loop_ctx>),
    "snapshot_session: actual minted writer loop-head PSH must remain "
    "the same size as the bare session handle.");
static_assert(
    sizeof(ReaderSession)
        == sizeof(SessionHandle<typename ReaderSession::protocol, ReaderHandle*, typename ReaderSession::loop_ctx>),
    "snapshot_session: actual minted reader loop-head PSH must remain "
    "the same size as the bare session handle.");

}  // namespace detail::snapshot_session_self_test

}  // namespace crucible::safety::proto::snapshot_session
