#pragma once

// ShardedCalendarGridSession.h - typed-session facade for per-shard
// calendar queues.
//
// PermissionedShardedCalendarGrid exposes one ProducerHandle<S> and one
// ConsumerHandle<S> for each shard S.  Each pair owns an independent
// priority-bucket calendar queue.  Slot identity is encoded by the
// item's key and the queue's bucket math, not by a separate handle type.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::sharded_calendar_grid_session {

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

template <typename Grid>
struct is_sharded_calendar_grid_session_surface : std::false_type {};

template <::crucible::concurrent::SpscValue T,
          std::size_t NumShards,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag>
struct is_sharded_calendar_grid_session_surface<
    ::crucible::concurrent::PermissionedShardedCalendarGrid<
        T, NumShards, NumBuckets, BucketCap, KeyExtractor,
        QuantumNs, UserTag>> : std::true_type {};

template <typename Grid>
concept ShardedCalendarGridSessionSurface =
    is_sharded_calendar_grid_session_surface<
        std::remove_cvref_t<Grid>>::value;

template <ShardedCalendarGridSessionSurface Grid, std::size_t S>
[[nodiscard]] auto mint_sharded_calendar_grid_producer(
    Grid& grid,
    ::crucible::safety::Permission<
        typename Grid::template shard_producer_tag<S>>&& perm) noexcept
{
    return grid.template producer<S>(std::move(perm));
}

template <ShardedCalendarGridSessionSurface Grid, std::size_t S>
[[nodiscard]] auto mint_sharded_calendar_grid_consumer(
    Grid& grid,
    ::crucible::safety::Permission<
        typename Grid::template shard_consumer_tag<S>>&& perm) noexcept
{
    return grid.template consumer<S>(std::move(perm));
}

template <ShardedCalendarGridSessionSurface Grid,
          std::size_t S,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_producer_session(Ctx const& ctx,
                      typename Grid::template ProducerHandle<S>& handle)
    noexcept
{
    using T = typename Grid::value_type;
    return mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <ShardedCalendarGridSessionSurface Grid,
          std::size_t S,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_consumer_session(Ctx const& ctx,
                      typename Grid::template ConsumerHandle<S>& handle)
    noexcept
{
    using T = typename Grid::value_type;
    return mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}

template <ShardedCalendarGridSessionSurface Grid,
          std::size_t S,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(
    mint_producer_session<Grid, S>(
        std::declval<Ctx const&>(),
        std::declval<typename Grid::template ProducerHandle<S>&>()));

template <ShardedCalendarGridSessionSurface Grid,
          std::size_t S,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle = decltype(
    mint_consumer_session<Grid, S>(
        std::declval<Ctx const&>(),
        std::declval<typename Grid::template ConsumerHandle<S>&>()));

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

template <ShardedCalendarGridSessionSurface Grid, std::size_t S>
[[nodiscard, gnu::always_inline]] inline bool producer_session_try_push(
    ProducerSessionHandle<Grid, S>& session,
    typename Grid::value_type const& value) noexcept
{
    return session.resource()->try_push(value);
}

template <ShardedCalendarGridSessionSurface Grid, std::size_t S>
[[nodiscard, gnu::always_inline]]
inline std::optional<typename Grid::value_type>
consumer_session_try_pop(ConsumerSessionHandle<Grid, S>& session) noexcept
{
    return session.resource()->try_pop();
}

namespace detail::sharded_calendar_grid_session_self_test {

struct Tag {};

struct Job {
    std::uint64_t deadline_ns = 0;
};

struct Key {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};

using Grid = ::crucible::concurrent::PermissionedShardedCalendarGrid<
    Job, 2, 8, 16, Key, 1'000'000ULL, Tag>;
using Producer0 = Grid::ProducerHandle<0>;
using Consumer1 = Grid::ConsumerHandle<1>;
using ProducerSession0 = ProducerSessionHandle<Grid, 0>;
using ConsumerSession1 = ConsumerSessionHandle<Grid, 1>;

static_assert(ShardedCalendarGridSessionSurface<Grid>);
static_assert(std::is_same_v<ProducerProto<Job>,
                             Loop<Send<Job, Continue>>>);
static_assert(std::is_same_v<ConsumerProto<Job>,
                             Loop<Recv<Job, Continue>>>);
static_assert(std::is_same_v<typename ProducerSession0::protocol,
                             Send<Job, Continue>>);
static_assert(std::is_same_v<typename ConsumerSession1::protocol,
                             Recv<Job, Continue>>);
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

}  // namespace detail::sharded_calendar_grid_session_self_test

}  // namespace crucible::safety::proto::sharded_calendar_grid_session
