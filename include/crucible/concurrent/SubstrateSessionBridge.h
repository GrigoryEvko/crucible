#pragma once

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/CalendarGridSession.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>
#include <crucible/sessions/ShardedGridSession.h>

#include <cstdint>
#include <source_location>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

enum class Direction : std::uint8_t {
    Producer = 0,
    Consumer = 1,
    SwmrWriter = 2,
    SwmrReader = 3,
    Owner = 4,
    Thief = 5,
};

template <std::size_t I>
struct ShardId {
    static constexpr std::size_t value = I;
};

template <std::size_t P>
struct CalendarProducerId {
    static constexpr std::size_t value = P;
};

struct CalendarConsumerId {
    static constexpr std::size_t value = 0;
};

template <class Substr, Direction Dir, class Shard = void>
struct handle_for;

template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = typename PermissionedSpscChannel<T, Cap, UserTag>::ProducerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = typename PermissionedSpscChannel<T, Cap, UserTag>::ConsumerHandle;
};

template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = typename PermissionedMpscChannel<T, Cap, UserTag>::ProducerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = typename PermissionedMpscChannel<T, Cap, UserTag>::ConsumerHandle;
};

// Only the handles of an open channel bridge to a session.  The closed-channel
// handles are terminal and have no protocol left to run.
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = typename PermissionedMpmcChannel<T, Cap, UserTag>::ProducerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = typename PermissionedMpmcChannel<T, Cap, UserTag>::ConsumerHandle;
};

template <class T, class UserTag>
struct handle_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrWriter> {
    using type = typename PermissionedSnapshot<T, UserTag>::WriterHandle;
};
template <class T, class UserTag>
struct handle_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrReader> {
    using type = typename PermissionedSnapshot<T, UserTag>::ReaderHandle;
};

template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedChaseLevDeque<T, Cap, UserTag>, Direction::Owner> {
    using type = typename PermissionedChaseLevDeque<T, Cap, UserTag>::OwnerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedChaseLevDeque<T, Cap, UserTag>, Direction::Thief> {
    using type = typename PermissionedChaseLevDeque<T, Cap, UserTag>::ThiefHandle;
};

template <class T, std::size_t M, std::size_t N, std::size_t Cap, class UserTag, class Routing, std::size_t I>
struct handle_for<PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>, Direction::Producer, ShardId<I>> {
    static_assert(I < M, "crucible::concurrent::diagnostic "
                         "[ShardedGridBridge_ProducerShardOutOfRange]: producer shard I "
                         "must be less than M.");
    using type = typename PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>::template ProducerHandle<I>;
};

template <class T, std::size_t M, std::size_t N, std::size_t Cap, class UserTag, class Routing, std::size_t J>
struct handle_for<PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>, Direction::Consumer, ShardId<J>> {
    static_assert(J < N, "crucible::concurrent::diagnostic "
                         "[ShardedGridBridge_ConsumerShardOutOfRange]: consumer shard J "
                         "must be less than N.");
    using type = typename PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>::template ConsumerHandle<J>;
};

// One producer handle per producer row, but a single consumer handle for the
// whole calendar, because the calendar drains through one consumer.
template <class T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t P>
struct handle_for<PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
                  Direction::Producer, CalendarProducerId<P>> {
    static_assert(P < NumProducers, "crucible::concurrent::diagnostic "
                                    "[CalendarGridBridge_ProducerRowOutOfRange]: producer row P "
                                    "must be less than NumProducers.");
    using type = typename PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs,
                                                   UserTag>::template ProducerHandle<P>;
};

template <class T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag>
struct handle_for<PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
                  Direction::Consumer, CalendarConsumerId> {
    using type = typename PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs,
                                                   UserTag>::ConsumerHandle;
};

// Handles are per shard.  The bucket a payload lands in follows from its key
// and affects only ordering, so it is not part of handle identity.
template <class T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t S>
struct handle_for<
    PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    Direction::Producer, ShardId<S>> {
    static_assert(S < NumShards, "crucible::concurrent::diagnostic "
                                 "[ShardedCalendarGridBridge_ShardOutOfRange]: shard S must be "
                                 "less than NumShards.");
    using type = typename PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs,
                                                          UserTag>::template ProducerHandle<S>;
};

template <class T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t S>
struct handle_for<
    PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    Direction::Consumer, ShardId<S>> {
    static_assert(S < NumShards, "crucible::concurrent::diagnostic "
                                 "[ShardedCalendarGridBridge_ShardOutOfRange]: shard S must be "
                                 "less than NumShards.");
    using type = typename PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs,
                                                          UserTag>::template ConsumerHandle<S>;
};

template <class Substr, Direction Dir, class Shard = void>
using handle_for_t = typename handle_for<Substr, Dir, Shard>::type;

// A loop with no exit branch is the deliberate spelling of a stream that runs
// until it is torn down.  Shutdown is a detach carrying a typed reason, not a
// branch the protocol offers.

template <class Substr, Direction Dir, class Shard = void>
struct default_proto_for;

template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Send<T, ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Send<T, ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Producer> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Send<T, ::crucible::safety::proto::Continue>>;
};

template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Recv<T, ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Recv<T, ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Recv<T, ::crucible::safety::proto::Continue>>;
};

// A publish types as a send and a load as a receive, even though the snapshot
// keeps only the newest value rather than a queue of them.
template <class T, class UserTag>
struct default_proto_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrWriter> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Send<T, ::crucible::safety::proto::Continue>>;
};

template <class T, class UserTag>
struct default_proto_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrReader> {
    using type =
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Recv<T, ::crucible::safety::proto::Continue>>;
};

// The owner protocol offers a choice between pushing and popping.
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedChaseLevDeque<T, Cap, UserTag>, Direction::Owner> {
    using type = ::crucible::safety::proto::chaselev_session::OwnerProto<T>;
};

// A thief only receives, and receives a borrow rather than ownership.
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedChaseLevDeque<T, Cap, UserTag>, Direction::Thief> {
    using type = ::crucible::safety::proto::chaselev_session::ThiefProto<
        T, typename PermissionedChaseLevDeque<T, Cap, UserTag>::thief_tag>;
};

template <class T, std::size_t M, std::size_t N, std::size_t Cap, class UserTag, class Routing, std::size_t I>
struct default_proto_for<PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>, Direction::Producer, ShardId<I>> {
    static_assert(I < M, "crucible::concurrent::diagnostic "
                         "[ShardedGridBridge_ProducerShardOutOfRange]: producer shard I "
                         "must be less than M.");
    using type = ::crucible::safety::proto::sharded_grid_session::ProducerProto<T>;
};

template <class T, std::size_t M, std::size_t N, std::size_t Cap, class UserTag, class Routing, std::size_t J>
struct default_proto_for<PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>, Direction::Consumer, ShardId<J>> {
    static_assert(J < N, "crucible::concurrent::diagnostic "
                         "[ShardedGridBridge_ConsumerShardOutOfRange]: consumer shard J "
                         "must be less than N.");
    using type = ::crucible::safety::proto::sharded_grid_session::ConsumerProto<T>;
};

template <class T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t P>
struct default_proto_for<
    PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    Direction::Producer, CalendarProducerId<P>> {
    static_assert(P < NumProducers, "crucible::concurrent::diagnostic "
                                    "[CalendarGridBridge_ProducerRowOutOfRange]: producer row P "
                                    "must be less than NumProducers.");
    using type = ::crucible::safety::proto::calendar_grid_session::ProducerProto<T>;
};

template <class T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag>
struct default_proto_for<
    PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    Direction::Consumer, CalendarConsumerId> {
    using type = ::crucible::safety::proto::calendar_grid_session::ConsumerProto<T>;
};

template <class T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t S>
struct default_proto_for<
    PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    Direction::Producer, ShardId<S>> {
    static_assert(S < NumShards, "crucible::concurrent::diagnostic "
                                 "[ShardedCalendarGridBridge_ShardOutOfRange]: shard S must be "
                                 "less than NumShards.");
    using type = ::crucible::safety::proto::sharded_calendar_grid_session::ProducerProto<T>;
};

template <class T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t S>
struct default_proto_for<
    PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    Direction::Consumer, ShardId<S>> {
    static_assert(S < NumShards, "crucible::concurrent::diagnostic "
                                 "[ShardedCalendarGridBridge_ShardOutOfRange]: shard S must be "
                                 "less than NumShards.");
    using type = ::crucible::safety::proto::sharded_calendar_grid_session::ConsumerProto<T>;
};

template <class Substr, Direction Dir, class Shard = void>
using default_proto_for_t = typename default_proto_for<Substr, Dir, Shard>::type;

namespace detail {

template <class Substr, Direction Dir>
concept HasHandleFor = requires { typename handle_for<Substr, Dir>::type; };

template <class Substr, Direction Dir>
concept HasDefaultProtoFor = requires { typename default_proto_for<Substr, Dir>::type; };

template <class Substr, class Shard, Direction Dir>
concept HasShardHandleFor = requires { typename handle_for<Substr, Dir, Shard>::type; };

template <class Substr, class Shard, Direction Dir>
concept HasShardDefaultProtoFor = requires { typename default_proto_for<Substr, Dir, Shard>::type; };

template <class Substr>
concept HasIndexedSessionSurface =
    ::crucible::safety::proto::sharded_grid_session::ShardedGridSessionSurface<Substr>
    || ::crucible::safety::proto::calendar_grid_session::CalendarGridSessionSurface<Substr>
    || ::crucible::safety::proto::sharded_calendar_grid_session::ShardedCalendarGridSessionSurface<Substr>;

template <class Substr, class Shard, Direction Dir>
struct shard_per_call_working_set;

template <class T, std::size_t M, std::size_t N, std::size_t Cap, class UserTag, class Routing, std::size_t I>
struct shard_per_call_working_set<PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>, ShardId<I>,
                                  Direction::Producer> {
    static constexpr std::size_t cell = ::crucible::concurrent::detail::cell_line_footprint(sizeof(T));
    static constexpr std::size_t value = 2 * ::crucible::concurrent::detail::kHotPathCacheLineBytes + cell;
};

template <class T, std::size_t M, std::size_t N, std::size_t Cap, class UserTag, class Routing, std::size_t J>
struct shard_per_call_working_set<PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>, ShardId<J>,
                                  Direction::Consumer> {
    static constexpr std::size_t cell = ::crucible::concurrent::detail::cell_line_footprint(sizeof(T));
    static constexpr std::size_t value = M * (2 * ::crucible::concurrent::detail::kHotPathCacheLineBytes + cell);
};

template <class T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t P>
struct shard_per_call_working_set<
    PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    CalendarProducerId<P>, Direction::Producer> {
    static constexpr std::size_t cell = ::crucible::concurrent::detail::cell_line_footprint(sizeof(T));
    static constexpr std::size_t value = 3 * ::crucible::concurrent::detail::kHotPathCacheLineBytes + cell;
};

template <class T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag>
struct shard_per_call_working_set<
    PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>,
    CalendarConsumerId, Direction::Consumer> {
    static constexpr std::size_t cell = ::crucible::concurrent::detail::cell_line_footprint(sizeof(T));
    static constexpr std::size_t value =
        ::crucible::concurrent::detail::kHotPathCacheLineBytes
        + NumBuckets * NumProducers * (2 * ::crucible::concurrent::detail::kHotPathCacheLineBytes + cell);
};

template <class T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t S>
struct shard_per_call_working_set<
    PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>, ShardId<S>,
    Direction::Producer> {
    static constexpr std::size_t cell = ::crucible::concurrent::detail::cell_line_footprint(sizeof(T));
    static constexpr std::size_t value = 3 * ::crucible::concurrent::detail::kHotPathCacheLineBytes + cell;
};

template <class T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap, class KeyExtractor,
          std::uint64_t QuantumNs, class UserTag, std::size_t S>
struct shard_per_call_working_set<
    PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>, ShardId<S>,
    Direction::Consumer> {
    static constexpr std::size_t cell = ::crucible::concurrent::detail::cell_line_footprint(sizeof(T));
    static constexpr std::size_t value =
        ::crucible::concurrent::detail::kHotPathCacheLineBytes
        + NumBuckets * (2 * ::crucible::concurrent::detail::kHotPathCacheLineBytes + cell);
};

}  // namespace detail

template <class Substr, Direction Dir>
concept IsBridgeableDirection =
    IsSubstrate<Substr> && detail::HasHandleFor<Substr, Dir> && detail::HasDefaultProtoFor<Substr, Dir>;

template <class Substr, class Shard, Direction Dir>
concept IsBridgeableShardDirection =
    detail::HasIndexedSessionSurface<Substr> && detail::HasShardHandleFor<Substr, Shard, Dir>
    && detail::HasShardDefaultProtoFor<Substr, Shard, Dir>;

template <class Substr, class Shard, Direction Dir, class Ctx>
concept ShardSubstrateFitsCtxResidency =
    ::crucible::effects::IsExecCtx<Ctx>
    && fits_in_tier_v<detail::shard_per_call_working_set<Substr, Shard, Dir>::value, ctx_residency_tier<Ctx>()>;

// A payload pinned to one vendor needs the matching loop context.  The default
// of void admits only payloads that name no vendor at all.
template <class Substr, Direction Dir, typename LoopCtx, typename Ctx>
concept CtxFitsSubstrateSessionMint =
    ::crucible::effects::IsExecCtx<Ctx> && IsBridgeableDirection<Substr, Dir> && SubstrateFitsCtxResidency<Substr, Ctx>
    && ::crucible::safety::proto::CtxFitsPermissionedProtocol<default_proto_for_t<Substr, Dir>, Ctx,
                                                              ::crucible::safety::proto::EmptyPermSet, LoopCtx>;

// The handle is taken by reference and its address forwarded.  It holds a
// reference to its channel, so it cannot be reseated and has to stay bound to
// the caller's scope.
template <class Substr, Direction Dir, typename LoopCtx = void, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSubstrateSessionMint<Substr, Dir, LoopCtx, Ctx>
[[nodiscard]] constexpr auto mint_substrate_session(Ctx const&, handle_for_t<Substr, Dir>& handle) noexcept {
    using Proto = default_proto_for_t<Substr, Dir>;
    using Handle = handle_for_t<Substr, Dir>;
    return ::crucible::safety::proto::detail::permissioned_session_with_loc_<
        Proto, ::crucible::safety::proto::EmptyPermSet, Handle*, LoopCtx>(&handle, std::source_location::current());
}

template <class Substr, class Shard, Direction Dir, typename LoopCtx, typename Ctx>
concept CtxFitsShardSubstrateSessionMint =
    ::crucible::effects::IsExecCtx<Ctx> && IsBridgeableShardDirection<Substr, Shard, Dir>
    && ShardSubstrateFitsCtxResidency<Substr, Shard, Dir, Ctx>
    && ::crucible::safety::proto::CtxFitsPermissionedProtocol<default_proto_for_t<Substr, Dir, Shard>, Ctx,
                                                              ::crucible::safety::proto::EmptyPermSet, LoopCtx>;

template <class Substr, class Shard, Direction Dir, typename LoopCtx = void, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsShardSubstrateSessionMint<Substr, Shard, Dir, LoopCtx, Ctx>
[[nodiscard]] constexpr auto mint_substrate_session(Ctx const&, handle_for_t<Substr, Dir, Shard>& handle) noexcept {
    using Proto = default_proto_for_t<Substr, Dir, Shard>;
    using Handle = handle_for_t<Substr, Dir, Shard>;
    return ::crucible::safety::proto::detail::permissioned_session_with_loc_<
        Proto, ::crucible::safety::proto::EmptyPermSet, Handle*, LoopCtx>(&handle, std::source_location::current());
}

namespace detail::substrate_session_bridge_self_test {

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct UserTag {};

using Spsc = PermissionedSpscChannel<int, 64, UserTag>;
using Mpsc = PermissionedMpscChannel<int, 64, UserTag>;
using Mpmc = PermissionedMpmcChannel<int, 64, UserTag>;
using SnapT = PermissionedSnapshot<int, UserTag>;
using DequeT = PermissionedChaseLevDeque<int, 64, UserTag>;
using GridT = PermissionedShardedGrid<int, 4, 8, 64, UserTag>;
struct CalendarKey {
    static std::uint64_t key(int value) noexcept { return static_cast<std::uint64_t>(value); }
};
using CalendarT = PermissionedCalendarGrid<int, 2, 64, 16, CalendarKey, 1ULL, UserTag>;
using ShardedCalendarT = PermissionedShardedCalendarGrid<int, 4, 64, 16, CalendarKey, 1ULL, UserTag>;
using NvInt = ::crucible::safety::Vendor<proto::VendorBackend::NV, int>;
using AmdInt = ::crucible::safety::Vendor<proto::VendorBackend::AMD, int>;
using NvSpsc = PermissionedSpscChannel<NvInt, 64, UserTag>;
using AmdSpsc = PermissionedSpscChannel<AmdInt, 64, UserTag>;

static_assert(std::is_same_v<handle_for_t<Spsc, Direction::Producer>, typename Spsc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Spsc, Direction::Consumer>, typename Spsc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpsc, Direction::Producer>, typename Mpsc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpsc, Direction::Consumer>, typename Mpsc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpmc, Direction::Producer>, typename Mpmc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpmc, Direction::Consumer>, typename Mpmc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<SnapT, Direction::SwmrWriter>, typename SnapT::WriterHandle>);
static_assert(std::is_same_v<handle_for_t<SnapT, Direction::SwmrReader>, typename SnapT::ReaderHandle>);
static_assert(std::is_same_v<handle_for_t<DequeT, Direction::Owner>, typename DequeT::OwnerHandle>);
static_assert(std::is_same_v<handle_for_t<DequeT, Direction::Thief>, typename DequeT::ThiefHandle>);
static_assert(
    std::is_same_v<handle_for_t<GridT, Direction::Producer, ShardId<2>>, typename GridT::template ProducerHandle<2>>);
static_assert(
    std::is_same_v<handle_for_t<GridT, Direction::Consumer, ShardId<7>>, typename GridT::template ConsumerHandle<7>>);
static_assert(std::is_same_v<handle_for_t<CalendarT, Direction::Producer, CalendarProducerId<1>>,
                             typename CalendarT::template ProducerHandle<1>>);
static_assert(std::is_same_v<handle_for_t<CalendarT, Direction::Consumer, CalendarConsumerId>,
                             typename CalendarT::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<ShardedCalendarT, Direction::Producer, ShardId<3>>,
                             typename ShardedCalendarT::template ProducerHandle<3>>);
static_assert(std::is_same_v<handle_for_t<ShardedCalendarT, Direction::Consumer, ShardId<3>>,
                             typename ShardedCalendarT::template ConsumerHandle<3>>);

static_assert(
    std::is_same_v<default_proto_for_t<Spsc, Direction::Producer>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Spsc, Direction::Consumer>, proto::Loop<proto::Recv<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Mpsc, Direction::Producer>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Mpmc, Direction::Consumer>, proto::Loop<proto::Recv<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<SnapT, Direction::SwmrWriter>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(std::is_same_v<default_proto_for_t<DequeT, Direction::Owner>, proto::chaselev_session::OwnerProto<int>>);
static_assert(std::is_same_v<default_proto_for_t<DequeT, Direction::Thief>,
                             proto::chaselev_session::ThiefProto<int, DequeT::thief_tag>>);
static_assert(std::is_same_v<default_proto_for_t<GridT, Direction::Producer, ShardId<2>>,
                             proto::sharded_grid_session::ProducerProto<int>>);
static_assert(std::is_same_v<default_proto_for_t<GridT, Direction::Consumer, ShardId<7>>,
                             proto::sharded_grid_session::ConsumerProto<int>>);
static_assert(std::is_same_v<default_proto_for_t<CalendarT, Direction::Producer, CalendarProducerId<1>>,
                             proto::calendar_grid_session::ProducerProto<int>>);
static_assert(std::is_same_v<default_proto_for_t<CalendarT, Direction::Consumer, CalendarConsumerId>,
                             proto::calendar_grid_session::ConsumerProto<int>>);
static_assert(std::is_same_v<default_proto_for_t<ShardedCalendarT, Direction::Producer, ShardId<3>>,
                             proto::sharded_calendar_grid_session::ProducerProto<int>>);
static_assert(std::is_same_v<default_proto_for_t<ShardedCalendarT, Direction::Consumer, ShardId<3>>,
                             proto::sharded_calendar_grid_session::ConsumerProto<int>>);

static_assert(IsBridgeableDirection<Spsc, Direction::Producer>);
static_assert(IsBridgeableDirection<Spsc, Direction::Consumer>);
static_assert(IsBridgeableDirection<SnapT, Direction::SwmrWriter>);
static_assert(IsBridgeableDirection<SnapT, Direction::SwmrReader>);
static_assert(IsBridgeableDirection<DequeT, Direction::Owner>);
static_assert(IsBridgeableDirection<DequeT, Direction::Thief>);
static_assert(IsBridgeableShardDirection<GridT, ShardId<0>, Direction::Producer>);
static_assert(IsBridgeableShardDirection<GridT, ShardId<7>, Direction::Consumer>);
static_assert(IsBridgeableShardDirection<CalendarT, CalendarProducerId<0>, Direction::Producer>);
static_assert(IsBridgeableShardDirection<CalendarT, CalendarConsumerId, Direction::Consumer>);
static_assert(IsBridgeableShardDirection<ShardedCalendarT, ShardId<0>, Direction::Producer>);
static_assert(IsBridgeableShardDirection<ShardedCalendarT, ShardId<3>, Direction::Consumer>);

static_assert(!IsBridgeableDirection<Spsc, Direction::SwmrWriter>);
static_assert(!IsBridgeableDirection<SnapT, Direction::Producer>);
static_assert(!IsBridgeableDirection<DequeT, Direction::Producer>);
static_assert(!IsBridgeableDirection<DequeT, Direction::Consumer>);
// The indexed families below are bridgeable only through an explicit shard or
// row index.  The unindexed form is deliberately left unbridged.
static_assert(!IsBridgeableDirection<GridT, Direction::Producer>);
static_assert(!IsBridgeableDirection<CalendarT, Direction::Producer>);
static_assert(!IsBridgeableDirection<CalendarT, Direction::Consumer>);
static_assert(!IsBridgeableDirection<ShardedCalendarT, Direction::Producer>);
static_assert(!IsBridgeableDirection<ShardedCalendarT, Direction::Consumer>);

static_assert(!IsBridgeableDirection<int, Direction::Producer>);

static_assert(!proto::CtxFitsPermissionedProtocol<default_proto_for_t<NvSpsc, Direction::Producer>, eff::HotFgCtx,
                                                  proto::EmptyPermSet>);
static_assert(proto::CtxFitsPermissionedProtocol<default_proto_for_t<NvSpsc, Direction::Producer>, eff::HotFgCtx,
                                                 proto::EmptyPermSet, proto::VendorCtx<proto::VendorBackend::NV>>);
static_assert(
    proto::CtxFitsPermissionedProtocol<default_proto_for_t<NvSpsc, Direction::Producer>, eff::HotFgCtx,
                                       proto::EmptyPermSet, proto::VendorCtx<proto::VendorBackend::Portable>>);
static_assert(!proto::CtxFitsPermissionedProtocol<default_proto_for_t<AmdSpsc, Direction::Consumer>, eff::HotFgCtx,
                                                  proto::EmptyPermSet, proto::VendorCtx<proto::VendorBackend::NV>>);

using NvProducerSession =
    decltype(mint_substrate_session<NvSpsc, Direction::Producer, proto::VendorCtx<proto::VendorBackend::NV>>(
        std::declval<eff::HotFgCtx const&>(), std::declval<handle_for_t<NvSpsc, Direction::Producer>&>()));
static_assert(NvProducerSession::vendor_backend == proto::VendorBackend::NV);

using DequeOwnerSession = decltype(mint_substrate_session<DequeT, Direction::Owner>(
    std::declval<eff::HotFgCtx const&>(), std::declval<handle_for_t<DequeT, Direction::Owner>&>()));
using DequeThiefSession = decltype(mint_substrate_session<DequeT, Direction::Thief>(
    std::declval<eff::HotFgCtx const&>(), std::declval<handle_for_t<DequeT, Direction::Thief>&>()));
static_assert(std::is_same_v<typename DequeOwnerSession::protocol,
                             proto::Select<proto::Send<int, proto::Continue>, proto::Recv<int, proto::Continue>>>);
static_assert(std::is_same_v<typename DequeThiefSession::protocol,
                             proto::Recv<proto::Borrowed<int, DequeT::thief_tag>, proto::Continue>>);

using GridProducerSession = decltype(mint_substrate_session<GridT, ShardId<2>, Direction::Producer>(
    std::declval<eff::HotFgCtx const&>(), std::declval<handle_for_t<GridT, Direction::Producer, ShardId<2>>&>()));
using GridConsumerSession = decltype(mint_substrate_session<GridT, ShardId<7>, Direction::Consumer>(
    std::declval<eff::HotFgCtx const&>(), std::declval<handle_for_t<GridT, Direction::Consumer, ShardId<7>>&>()));
static_assert(std::is_same_v<typename GridProducerSession::protocol, proto::Send<int, proto::Continue>>);
static_assert(std::is_same_v<typename GridConsumerSession::protocol, proto::Recv<int, proto::Continue>>);

using CalendarProducerSession = decltype(mint_substrate_session<CalendarT, CalendarProducerId<1>, Direction::Producer>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<handle_for_t<CalendarT, Direction::Producer, CalendarProducerId<1>>&>()));
using CalendarConsumerSession = decltype(mint_substrate_session<CalendarT, CalendarConsumerId, Direction::Consumer>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<handle_for_t<CalendarT, Direction::Consumer, CalendarConsumerId>&>()));
static_assert(std::is_same_v<typename CalendarProducerSession::protocol, proto::Send<int, proto::Continue>>);
static_assert(std::is_same_v<typename CalendarConsumerSession::protocol, proto::Recv<int, proto::Continue>>);

using ShardedCalendarProducerSession =
    decltype(mint_substrate_session<ShardedCalendarT, ShardId<3>, Direction::Producer>(
        std::declval<eff::HotFgCtx const&>(),
        std::declval<handle_for_t<ShardedCalendarT, Direction::Producer, ShardId<3>>&>()));
using ShardedCalendarConsumerSession =
    decltype(mint_substrate_session<ShardedCalendarT, ShardId<3>, Direction::Consumer>(
        std::declval<eff::HotFgCtx const&>(),
        std::declval<handle_for_t<ShardedCalendarT, Direction::Consumer, ShardId<3>>&>()));
static_assert(std::is_same_v<typename ShardedCalendarProducerSession::protocol, proto::Send<int, proto::Continue>>);
static_assert(std::is_same_v<typename ShardedCalendarConsumerSession::protocol, proto::Recv<int, proto::Continue>>);

}  // namespace detail::substrate_session_bridge_self_test

}  // namespace crucible::concurrent
