#pragma once

// CalendarGridSession.h - typed-session facade for CalendarGrid rows.
//
// PermissionedCalendarGrid is a priority-bucket calendar queue:
//   Producer<P>: writes into one statically-indexed producer row.
//   Consumer:    drains the whole M x NumBuckets queue in priority order.
//
// The live substrate reports "no currently observable event" as
// std::optional<T>::nullopt from ConsumerHandle::try_pop().  It is not a
// per-slot presence table, so this facade preserves that exact surface
// instead of inventing a separate SlotMissed protocol above the queue.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedCalendarGrid.h>
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

namespace crucible::safety::proto::calendar_grid_session {

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

template <typename Grid>
struct is_calendar_grid_session_surface : std::false_type {};

template <::crucible::concurrent::SpscValue T,
          std::size_t NumProducers,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag>
struct is_calendar_grid_session_surface<
    ::crucible::concurrent::PermissionedCalendarGrid<
        T, NumProducers, NumBuckets, BucketCap, KeyExtractor,
        QuantumNs, UserTag>> : std::true_type {};

template <typename Grid>
concept CalendarGridSessionSurface =
    is_calendar_grid_session_surface<std::remove_cvref_t<Grid>>::value;

template <CalendarGridSessionSurface Grid, std::size_t P>
[[nodiscard]] auto mint_calendar_grid_producer(
    Grid& grid,
    ::crucible::safety::Permission<
        ::crucible::concurrent::calendar_tag::Producer<
            typename Grid::user_tag, P>>&& perm) noexcept
{
    return grid.template producer<P>(std::move(perm));
}

template <CalendarGridSessionSurface Grid>
[[nodiscard]] auto mint_calendar_grid_consumer(
    Grid& grid,
    ::crucible::safety::Permission<
        ::crucible::concurrent::calendar_tag::Consumer<
            typename Grid::user_tag>>&& perm) noexcept
{
    return grid.consumer(std::move(perm));
}

template <CalendarGridSessionSurface Grid,
          std::size_t P,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_producer_session(Ctx const& ctx,
                      typename Grid::template ProducerHandle<P>& handle)
    noexcept
{
    using T = typename Grid::value_type;
    return mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <CalendarGridSessionSurface Grid,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_consumer_session(Ctx const& ctx,
                      typename Grid::ConsumerHandle& handle) noexcept
{
    using T = typename Grid::value_type;
    return mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}

template <CalendarGridSessionSurface Grid,
          std::size_t P,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(
    mint_producer_session<Grid, P>(
        std::declval<Ctx const&>(),
        std::declval<typename Grid::template ProducerHandle<P>&>()));

template <CalendarGridSessionSurface Grid,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle = decltype(
    mint_consumer_session<Grid>(
        std::declval<Ctx const&>(),
        std::declval<typename Grid::ConsumerHandle&>()));

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

template <CalendarGridSessionSurface Grid, std::size_t P>
[[nodiscard, gnu::always_inline]] inline bool producer_session_try_push(
    ProducerSessionHandle<Grid, P>& session,
    typename Grid::value_type const& value) noexcept
{
    return session.resource()->try_push(value);
}

template <CalendarGridSessionSurface Grid>
[[nodiscard, gnu::always_inline]]
inline std::optional<typename Grid::value_type>
consumer_session_try_pop(ConsumerSessionHandle<Grid>& session) noexcept
{
    return session.resource()->try_pop();
}

namespace detail::calendar_grid_session_self_test {

struct Tag {};

struct Job {
    std::uint64_t deadline_ns = 0;
};

struct Key {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};

using Grid = ::crucible::concurrent::PermissionedCalendarGrid<
    Job, 2, 8, 16, Key, 1'000'000ULL, Tag>;
using Producer0 = Grid::ProducerHandle<0>;
using Consumer = Grid::ConsumerHandle;
using ProducerSession0 = ProducerSessionHandle<Grid, 0>;
using ConsumerSession = ConsumerSessionHandle<Grid>;

static_assert(CalendarGridSessionSurface<Grid>);
static_assert(std::is_same_v<ProducerProto<Job>,
                             Loop<Send<Job, Continue>>>);
static_assert(std::is_same_v<ConsumerProto<Job>,
                             Loop<Recv<Job, Continue>>>);
static_assert(std::is_same_v<typename ProducerSession0::protocol,
                             Send<Job, Continue>>);
static_assert(std::is_same_v<typename ConsumerSession::protocol,
                             Recv<Job, Continue>>);
static_assert(std::is_same_v<typename ProducerSession0::perm_set,
                             EmptyPermSet>);
static_assert(std::is_same_v<typename ConsumerSession::perm_set,
                             EmptyPermSet>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                Producer0*>)
              == sizeof(SessionHandle<End, Producer0*>));
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                Consumer*>)
              == sizeof(SessionHandle<End, Consumer*>));
static_assert(sizeof(ProducerSession0)
              == sizeof(SessionHandle<typename ProducerSession0::protocol,
                                      Producer0*,
                                      typename ProducerSession0::loop_ctx>));
static_assert(sizeof(ConsumerSession)
              == sizeof(SessionHandle<typename ConsumerSession::protocol,
                                      Consumer*,
                                      typename ConsumerSession::loop_ctx>));

}  // namespace detail::calendar_grid_session_self_test

}  // namespace crucible::safety::proto::calendar_grid_session
