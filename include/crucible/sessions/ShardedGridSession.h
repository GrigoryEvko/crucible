#pragma once

// ── ShardedGridSession.h — typed-session facade for ShardedGrid shards ─
//
// PermissionedShardedGrid exposes M statically-indexed producers and N
// statically-indexed consumers.  This header gives each slot the same
// infinite streaming session shape used by SPSC:
//
//   Producer<I>: Loop<Send<T, Continue>>
//   Consumer<J>: Loop<Recv<T, Continue>>
//
// Shard identity remains in the handle type, not in the payload and not
// in a runtime integer.  That is the point of this facade: session
// composition can talk about producer shard I or consumer shard J without
// weakening the underlying linear permission split.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::sharded_grid_session {

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

template <typename Grid>
struct is_sharded_grid_session_surface : std::false_type {};

template <::crucible::concurrent::SpscValue T,
          std::size_t M,
          std::size_t N,
          std::size_t Capacity,
          typename UserTag,
          typename Routing>
struct is_sharded_grid_session_surface<
    ::crucible::concurrent::PermissionedShardedGrid<
        T, M, N, Capacity, UserTag, Routing>> : std::true_type {};

template <typename Grid>
concept ShardedGridSessionSurface =
    is_sharded_grid_session_surface<std::remove_cvref_t<Grid>>::value;

template <ShardedGridSessionSurface Grid, std::size_t I>
[[nodiscard]] constexpr auto mint_sharded_grid_producer(
    Grid& grid,
    ::crucible::safety::Permission<
        ::crucible::concurrent::grid_tag::Producer<
            typename Grid::user_tag, I>>&& perm) noexcept
{
    return grid.template producer<I>(std::move(perm));
}

template <ShardedGridSessionSurface Grid, std::size_t J>
[[nodiscard]] constexpr auto mint_sharded_grid_consumer(
    Grid& grid,
    ::crucible::safety::Permission<
        ::crucible::concurrent::grid_tag::Consumer<
            typename Grid::user_tag, J>>&& perm) noexcept
{
    return grid.template consumer<J>(std::move(perm));
}

template <ShardedGridSessionSurface Grid,
          std::size_t I,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_producer_session(Ctx const& ctx,
                      typename Grid::template ProducerHandle<I>& handle) noexcept
{
    using T = typename Grid::value_type;
    return mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <ShardedGridSessionSurface Grid,
          std::size_t J,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_consumer_session(Ctx const& ctx,
                      typename Grid::template ConsumerHandle<J>& handle) noexcept
{
    using T = typename Grid::value_type;
    return mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}

template <ShardedGridSessionSurface Grid,
          std::size_t I,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(
    mint_producer_session<Grid, I>(std::declval<Ctx const&>(),
        std::declval<typename Grid::template ProducerHandle<I>&>()));

template <ShardedGridSessionSurface Grid,
          std::size_t J,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle = decltype(
    mint_consumer_session<Grid, J>(std::declval<Ctx const&>(),
        std::declval<typename Grid::template ConsumerHandle<J>&>()));

inline constexpr auto blocking_push =
    [](auto& hp, auto&& value) noexcept {
        while (!hp->try_push(std::forward<decltype(value)>(value))) {
            CRUCIBLE_SPIN_PAUSE;
        }
    };

inline constexpr auto blocking_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        CRUCIBLE_SPIN_PAUSE;
    }
};

template <ShardedGridSessionSurface Grid, std::size_t I>
[[nodiscard, gnu::always_inline]] inline bool producer_session_try_push(
    ProducerSessionHandle<Grid, I>& session,
    typename Grid::value_type value) noexcept
{
    return session.resource()->try_push(value);
}

template <ShardedGridSessionSurface Grid, std::size_t J>
[[nodiscard, gnu::always_inline]]
inline std::optional<typename Grid::value_type>
consumer_session_try_pop(ConsumerSessionHandle<Grid, J>& session) noexcept
{
    return session.resource()->try_pop();
}

namespace detail::sharded_grid_session_self_test {

struct Tag {};
using Grid = ::crucible::concurrent::PermissionedShardedGrid<int, 2, 2, 16, Tag>;
using Producer0 = Grid::ProducerHandle<0>;
using Consumer1 = Grid::ConsumerHandle<1>;
using ProducerSession0 = ProducerSessionHandle<Grid, 0>;
using ConsumerSession1 = ConsumerSessionHandle<Grid, 1>;

static_assert(ShardedGridSessionSurface<Grid>);
static_assert(std::is_same_v<ProducerProto<int>,
                             Loop<Send<int, Continue>>>);
static_assert(std::is_same_v<ConsumerProto<int>,
                             Loop<Recv<int, Continue>>>);
static_assert(std::is_same_v<typename ProducerSession0::protocol,
                             Send<int, Continue>>);
static_assert(std::is_same_v<typename ConsumerSession1::protocol,
                             Recv<int, Continue>>);
static_assert(std::is_same_v<typename ProducerSession0::perm_set,
                             EmptyPermSet>);
static_assert(std::is_same_v<typename ConsumerSession1::perm_set,
                             EmptyPermSet>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                Producer0*>)
              == sizeof(SessionHandle<End, Producer0*>));
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                Consumer1*>)
              == sizeof(SessionHandle<End, Consumer1*>));
static_assert(sizeof(ProducerSession0)
              == sizeof(SessionHandle<typename ProducerSession0::protocol,
                                      Producer0*,
                                      typename ProducerSession0::loop_ctx>));
static_assert(sizeof(ConsumerSession1)
              == sizeof(SessionHandle<typename ConsumerSession1::protocol,
                                      Consumer1*,
                                      typename ConsumerSession1::loop_ctx>));

}  // namespace detail::sharded_grid_session_self_test

}  // namespace crucible::safety::proto::sharded_grid_session
