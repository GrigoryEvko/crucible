#pragma once

// This namespace surfaces every substrate session factory.  A caller
// who holds a handle into one substrate never has to pick the matching
// session header for it.
//
// Each substrate sub-namespace offers the same two shapes.  An endpoint
// mint takes the substrate and a permission, and hands back a typed
// role handle.  A session mint takes an execution context as well, and
// wraps that handle for cross-tier composition.
//
// A re-export here carries the substrate's own concept gate, its
// qualifiers and its role duality unchanged.  Where this file defines a
// mint of its own instead of a re-export, that mint holds to the same
// shape.

#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/sessions/CalendarGridSession.h>
#include <crucible/sessions/ChainEdgeSession.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
#include <crucible/sessions/MetaLogSession.h>
#include <crucible/sessions/MpmcChannelSession.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>
#include <crucible/sessions/ShardedGridSession.h>
#include <crucible/sessions/SnapshotSession.h>
#include <crucible/sessions/SpscSession.h>
#include <crucible/sessions/SwmrSession.h>

#include <type_traits>

namespace crucible::fixy::substr {

namespace spsc {
template <typename T>
using ProducerProto = ::crucible::safety::proto::spsc_session::ProducerProto<T>;

template <typename T>
using ConsumerProto = ::crucible::safety::proto::spsc_session::ConsumerProto<T>;

template <::crucible::concurrent::SpscValue T, std::size_t Capacity, typename UserTag = void>
using PermissionedSpscChannel = ::crucible::concurrent::PermissionedSpscChannel<T, Capacity, UserTag>;

using ::crucible::concurrent::SpscValue;

namespace spsc_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::spsc_tag::Whole<UserTag>;
template <typename UserTag>
using Producer = ::crucible::concurrent::spsc_tag::Producer<UserTag>;
template <typename UserTag>
using Consumer = ::crucible::concurrent::spsc_tag::Consumer<UserTag>;
}  // namespace spsc_tag

// Both endpoint factories hand back a bare handle rather than an
// optional.  The channel admits exactly one producer and exactly one
// consumer.  Each permission is linear, and neither factory contends
// for a pooled slot or fails.
template <typename Channel>
concept SpscChannelSessionSurface = requires(Channel& ch, typename Channel::ProducerHandle& producer_handle,
                                             typename Channel::ConsumerHandle& consumer_handle,
                                             ::crucible::safety::Permission<typename Channel::producer_tag>&& prod_perm,
                                             ::crucible::safety::Permission<typename Channel::consumer_tag>&& cons_perm,
                                             const typename Channel::value_type& sample_payload) {
    typename Channel::value_type;
    typename Channel::user_tag;
    typename Channel::whole_tag;
    typename Channel::producer_tag;
    typename Channel::consumer_tag;
    typename Channel::ProducerHandle;
    typename Channel::ConsumerHandle;

    { ch.producer(std::move(prod_perm)) } -> std::same_as<typename Channel::ProducerHandle>;
    { ch.consumer(std::move(cons_perm)) } -> std::same_as<typename Channel::ConsumerHandle>;

    { producer_handle.try_push(sample_payload) } -> std::same_as<bool>;
    { consumer_handle.try_pop() } -> std::same_as<std::optional<typename Channel::value_type>>;
};

// This file defines these two rather than a re-export, because the
// substrate ships only the context-bound session mints.  The shape
// matches the endpoint mints that every other sub-namespace re-exports.

template <SpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto
mint_spsc_producer_endpoint(Channel& ch, ::crucible::safety::Permission<typename Channel::producer_tag>&& perm) noexcept
    -> typename Channel::ProducerHandle {
    return ch.producer(std::move(perm));
}

template <SpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto
mint_spsc_consumer_endpoint(Channel& ch, ::crucible::safety::Permission<typename Channel::consumer_tag>&& perm) noexcept
    -> typename Channel::ConsumerHandle {
    return ch.consumer(std::move(perm));
}

using ::crucible::safety::proto::spsc_session::mint_producer_session;
using ::crucible::safety::proto::spsc_session::mint_consumer_session;
}  // namespace spsc

namespace swmr {
template <typename T>
using WriterProto = ::crucible::safety::proto::swmr_session::WriterProto<T>;

template <typename T, typename ReaderTag>
using ReaderProto = ::crucible::safety::proto::swmr_session::ReaderProto<T, ReaderTag>;

template <typename T>
using WriterRuntimeProto = ::crucible::safety::proto::swmr_session::WriterRuntimeProto<T>;

template <typename T>
using ReaderRuntimeProto = ::crucible::safety::proto::swmr_session::ReaderRuntimeProto<T>;

template <typename S>
concept SwmrSessionSurface = ::crucible::safety::proto::swmr_session::SwmrSessionSurface<S>;

template <typename T, typename WriterTag, typename ReaderTag>
using SwmrSession = ::crucible::safety::proto::swmr_session::SwmrSession<T, WriterTag, ReaderTag>;

using ::crucible::safety::proto::swmr_session::mint_swmr_writer;
using ::crucible::safety::proto::swmr_session::mint_swmr_reader;
using ::crucible::safety::proto::swmr_session::mint_writer_session;
using ::crucible::safety::proto::swmr_session::mint_reader_session;
using ::crucible::safety::proto::swmr_session::mint_writer_runtime_session;
using ::crucible::safety::proto::swmr_session::mint_reader_runtime_session;
}  // namespace swmr

namespace chaselev {
template <typename T>
using OwnerProto = ::crucible::safety::proto::chaselev_session::OwnerProto<T>;

template <typename T, typename ThiefTag>
using ThiefProto = ::crucible::safety::proto::chaselev_session::ThiefProto<T, ThiefTag>;

template <typename Deque>
concept ChaseLevSessionSurface = ::crucible::safety::proto::chaselev_session::ChaseLevSessionSurface<Deque>;

// The deque splits one linear owner against many fractional thieves.
// The substrate builds the thief pool itself, so a caller mints only
// the owner permission.

template <::crucible::concurrent::DequeValue T, std::size_t Capacity, typename UserTag = void>
using PermissionedChaseLevDeque = ::crucible::concurrent::PermissionedChaseLevDeque<T, Capacity, UserTag>;

using ::crucible::concurrent::DequeValue;

// The substrate also admits an explicit split of the whole permission
// into owner and thief.  The thief tag stays in reach here for a
// caller who wants to build that pool itself.
namespace deque_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::deque_tag::Whole<UserTag>;
template <typename UserTag>
using Owner = ::crucible::concurrent::deque_tag::Owner<UserTag>;
template <typename UserTag>
using Thief = ::crucible::concurrent::deque_tag::Thief<UserTag>;
}  // namespace deque_tag

using ::crucible::safety::proto::chaselev_session::mint_chaselev_owner;
using ::crucible::safety::proto::chaselev_session::mint_chaselev_thief;
using ::crucible::safety::proto::chaselev_session::mint_owner_session;
using ::crucible::safety::proto::chaselev_session::mint_thief_session;
}  // namespace chaselev

namespace metalog {
using MetaLogRecord = ::crucible::safety::proto::metalog_session::MetaLogRecord;

using ProducerProto = ::crucible::safety::proto::metalog_session::ProducerProto;

using ConsumerProto = ::crucible::safety::proto::metalog_session::ConsumerProto;

template <typename Log>
concept MetaLogSessionSurface = ::crucible::safety::proto::metalog_session::MetaLogSessionSurface<Log>;

template <typename UserTag = void>
using PermissionedMetaLog = ::crucible::concurrent::PermissionedMetaLog<UserTag>;

using ::crucible::MetaIndex;

namespace metalog_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::metalog_tag::Whole<UserTag>;
template <typename UserTag>
using Producer = ::crucible::concurrent::metalog_tag::Producer<UserTag>;
template <typename UserTag>
using Consumer = ::crucible::concurrent::metalog_tag::Consumer<UserTag>;
}  // namespace metalog_tag

using ::crucible::safety::proto::metalog_session::mint_metalog_producer;
using ::crucible::safety::proto::metalog_session::mint_metalog_consumer;
using ::crucible::safety::proto::metalog_session::mint_metalog_producer_session;
using ::crucible::safety::proto::metalog_session::mint_metalog_consumer_session;
}  // namespace metalog

namespace chainedge {
using Signal = ::crucible::safety::proto::chainedge_session::Signal;
using SignalerProto = ::crucible::safety::proto::chainedge_session::SignalerProto;
using WaiterProto = ::crucible::safety::proto::chainedge_session::WaiterProto;

template <typename Edge>
concept ChainEdgeSessionSurface = ::crucible::safety::proto::chainedge_session::ChainEdgeSessionSurface<Edge>;

template <::crucible::concurrent::VendorBackend Backend = ::crucible::concurrent::VendorBackend::CPU,
          typename UserTag = void>
using PermissionedChainEdge = ::crucible::concurrent::PermissionedChainEdge<Backend, UserTag>;

using ::crucible::concurrent::VendorBackend;

namespace chainedge_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::chainedge_tag::Whole<UserTag>;
template <typename UserTag>
using Signaler = ::crucible::concurrent::chainedge_tag::Signaler<UserTag>;
template <typename UserTag>
using Waiter = ::crucible::concurrent::chainedge_tag::Waiter<UserTag>;
}  // namespace chainedge_tag

using ::crucible::safety::proto::chainedge_session::mint_chainedge_signaler;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_waiter;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_signaler_session;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_waiter_session;
}  // namespace chainedge

namespace mpmc {
template <typename T>
using ProducerProto = ::crucible::safety::proto::mpmc_channel_session::ProducerProto<T>;

template <typename T>
using ConsumerProto = ::crucible::safety::proto::mpmc_channel_session::ConsumerProto<T>;

template <typename Channel>
concept MpmcChannelSessionSurface = ::crucible::safety::proto::mpmc_channel_session::MpmcChannelSessionSurface<Channel>;

template <typename T, std::size_t Capacity, typename UserTag = void>
using PermissionedMpmcChannel = ::crucible::concurrent::PermissionedMpmcChannel<T, Capacity, UserTag>;

using ::crucible::concurrent::MpmcValue;

namespace mpmc_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::mpmc_tag::Whole<UserTag>;
template <typename UserTag>
using Producer = ::crucible::concurrent::mpmc_tag::Producer<UserTag>;
template <typename UserTag>
using Consumer = ::crucible::concurrent::mpmc_tag::Consumer<UserTag>;
}  // namespace mpmc_tag

using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_producer_endpoint;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_consumer_endpoint;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_producer_session;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_consumer_session;
}  // namespace mpmc

namespace calendar_grid {
template <typename T>
using ProducerProto = ::crucible::safety::proto::calendar_grid_session::ProducerProto<T>;

template <typename T>
using ConsumerProto = ::crucible::safety::proto::calendar_grid_session::ConsumerProto<T>;

template <typename Grid>
concept CalendarGridSessionSurface = ::crucible::safety::proto::calendar_grid_session::CalendarGridSessionSurface<Grid>;

template <::crucible::concurrent::SpscValue T, std::size_t NumProducers, std::size_t NumBuckets, std::size_t BucketCap,
          typename KeyExtractor, std::uint64_t QuantumNs, typename UserTag = void>
using PermissionedCalendarGrid =
    ::crucible::concurrent::PermissionedCalendarGrid<T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs,
                                                     UserTag>;

using ::crucible::concurrent::KeyExtractorOf;

namespace calendar_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::calendar_tag::Whole<UserTag>;
template <typename UserTag, std::size_t P>
using Producer = ::crucible::concurrent::calendar_tag::Producer<UserTag, P>;
template <typename UserTag>
using Consumer = ::crucible::concurrent::calendar_tag::Consumer<UserTag>;
}  // namespace calendar_tag

using ::crucible::safety::proto::calendar_grid_session::mint_calendar_grid_producer;
using ::crucible::safety::proto::calendar_grid_session::mint_calendar_grid_consumer;
using ::crucible::safety::proto::calendar_grid_session::mint_producer_session;
using ::crucible::safety::proto::calendar_grid_session::mint_consumer_session;
}  // namespace calendar_grid

namespace sharded_calendar_grid {
template <typename T>
using ProducerProto = ::crucible::safety::proto::sharded_calendar_grid_session::ProducerProto<T>;

template <typename T>
using ConsumerProto = ::crucible::safety::proto::sharded_calendar_grid_session::ConsumerProto<T>;

template <typename Grid>
concept ShardedCalendarGridSessionSurface =
    ::crucible::safety::proto::sharded_calendar_grid_session::ShardedCalendarGridSessionSurface<Grid>;

template <::crucible::concurrent::SpscValue T, std::size_t NumShards, std::size_t NumBuckets, std::size_t BucketCap,
          typename KeyExtractor, std::uint64_t QuantumNs, typename UserTag = void>
using PermissionedShardedCalendarGrid =
    ::crucible::concurrent::PermissionedShardedCalendarGrid<T, NumShards, NumBuckets, BucketCap, KeyExtractor,
                                                            QuantumNs, UserTag>;

using ::crucible::concurrent::ShardedCalendarKeyExtractorOf;

namespace sharded_calendar_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::sharded_calendar_tag::Whole<UserTag>;
template <typename UserTag, std::size_t S>
using Producer = ::crucible::concurrent::sharded_calendar_tag::Producer<UserTag, S>;
template <typename UserTag, std::size_t S>
using Consumer = ::crucible::concurrent::sharded_calendar_tag::Consumer<UserTag, S>;
}  // namespace sharded_calendar_tag

using ::crucible::safety::proto::sharded_calendar_grid_session::mint_sharded_calendar_grid_producer;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_sharded_calendar_grid_consumer;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_producer_session;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_consumer_session;
}  // namespace sharded_calendar_grid

namespace sharded_grid {
template <typename T>
using ProducerProto = ::crucible::safety::proto::sharded_grid_session::ProducerProto<T>;

template <typename T>
using ConsumerProto = ::crucible::safety::proto::sharded_grid_session::ConsumerProto<T>;

template <typename Grid>
concept ShardedGridSessionSurface = ::crucible::safety::proto::sharded_grid_session::ShardedGridSessionSurface<Grid>;

template <::crucible::concurrent::SpscValue T, std::size_t M, std::size_t N, std::size_t Capacity,
          typename UserTag = void, typename Routing = ::crucible::concurrent::RoundRobinRouting>
using PermissionedShardedGrid = ::crucible::concurrent::PermissionedShardedGrid<T, M, N, Capacity, UserTag, Routing>;

using ::crucible::concurrent::RoundRobinRouting;
using ::crucible::concurrent::HashKeyRouting;
using ::crucible::concurrent::AffinityRouting;

namespace grid_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::grid_tag::Whole<UserTag>;
template <typename UserTag, std::size_t I>
using Producer = ::crucible::concurrent::grid_tag::Producer<UserTag, I>;
template <typename UserTag, std::size_t J>
using Consumer = ::crucible::concurrent::grid_tag::Consumer<UserTag, J>;
}  // namespace grid_tag

using ::crucible::safety::proto::sharded_grid_session::mint_sharded_grid_producer;
using ::crucible::safety::proto::sharded_grid_session::mint_sharded_grid_consumer;
using ::crucible::safety::proto::sharded_grid_session::mint_producer_session;
using ::crucible::safety::proto::sharded_grid_session::mint_consumer_session;
}  // namespace sharded_grid

namespace snapshot {
template <::crucible::concurrent::SnapshotValue T, typename UserTag = void>
using PermissionedSnapshot = ::crucible::concurrent::PermissionedSnapshot<T, UserTag>;

template <typename T>
using WriterProto = ::crucible::safety::proto::snapshot_session::WriterProto<T>;

template <typename T>
using ReaderProto = ::crucible::safety::proto::snapshot_session::ReaderProto<T>;

template <typename Snap>
concept SnapshotSessionSurface = ::crucible::safety::proto::snapshot_session::SnapshotSessionSurface<Snap>;

using ::crucible::safety::proto::snapshot_session::mint_snapshot_writer;
using ::crucible::safety::proto::snapshot_session::mint_snapshot_reader;
using ::crucible::safety::proto::snapshot_session::mint_snapshot_writer_session;
using ::crucible::safety::proto::snapshot_session::mint_snapshot_reader_session;
}  // namespace snapshot

// The substrate ships an MPSC channel but no typed-session header for
// it.  The MPMC shape, with both endpoints pooled, does not carry over
// to a linear consumer against a pooled producer.  This file defines
// the namespace instead of a re-export.
//
// The protocols match the generic substrate bridge exactly.  The
// surface concept exists so that a bad call site reports an
// MPSC-shaped failure instead of a generic bridge failure.

namespace mpsc {
template <::crucible::concurrent::RingValue T, std::size_t Capacity, typename UserTag = void>
using PermissionedMpscChannel = ::crucible::concurrent::PermissionedMpscChannel<T, Capacity, UserTag>;

template <typename T>
using ProducerProto =
    ::crucible::safety::proto::Loop<::crucible::safety::proto::Send<T, ::crucible::safety::proto::Continue>>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::Loop<::crucible::safety::proto::Recv<T, ::crucible::safety::proto::Continue>>;

// The consumer factory hands back a bare handle because the ring
// admits one consumer, so its permission is linear.  The producer
// factory is pooled and can fail, so it hands back an optional.
template <typename Channel>
concept MpscChannelSessionSurface = requires(Channel& ch, typename Channel::ProducerHandle& producer_handle,
                                             typename Channel::ConsumerHandle& consumer_handle,
                                             ::crucible::safety::Permission<typename Channel::consumer_tag>&& cons_perm,
                                             const typename Channel::value_type& sample_payload) {
    typename Channel::value_type;
    typename Channel::user_tag;
    typename Channel::producer_tag;
    typename Channel::consumer_tag;
    typename Channel::ProducerHandle;
    typename Channel::ConsumerHandle;

    { ch.producer() } -> std::same_as<std::optional<typename Channel::ProducerHandle>>;
    { ch.consumer(std::move(cons_perm)) } -> std::same_as<typename Channel::ConsumerHandle>;

    { producer_handle.try_push(sample_payload) } -> std::same_as<bool>;
    { consumer_handle.try_pop() } -> std::same_as<std::optional<typename Channel::value_type>>;
};

template <MpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto mint_mpsc_producer_endpoint(Channel& ch) noexcept
    -> std::optional<typename Channel::ProducerHandle> {
    return ch.producer();
}

template <MpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto
mint_mpsc_consumer_endpoint(Channel& ch, ::crucible::safety::Permission<typename Channel::consumer_tag>&& perm) noexcept
    -> typename Channel::ConsumerHandle {
    return ch.consumer(std::move(perm));
}

// These take the handle by reference and forward its address as the
// session resource.  The permission set is empty: producer and consumer
// authority already lives on the handle itself, so nothing travels over
// the wire.

template <MpscChannelSessionSurface Channel, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_mpsc_producer_session(Ctx const& ctx,
                                                        typename Channel::ProducerHandle& handle) noexcept {
    using T = typename Channel::value_type;
    return ::crucible::safety::proto::mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <MpscChannelSessionSurface Channel, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_mpsc_consumer_session(Ctx const& ctx,
                                                        typename Channel::ConsumerHandle& handle) noexcept {
    using T = typename Channel::value_type;
    return ::crucible::safety::proto::mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}
}  // namespace mpsc

using ::crucible::concurrent::mint_substrate_session;

using ::crucible::concurrent::ChannelTopology;
using ::crucible::concurrent::Substrate;
using ::crucible::concurrent::Substrate_t;

using ::crucible::concurrent::substrate_traits;
using ::crucible::concurrent::is_substrate;
using ::crucible::concurrent::is_substrate_v;
using ::crucible::concurrent::substrate_topology_v;
using ::crucible::concurrent::substrate_value_type_t;
using ::crucible::concurrent::substrate_user_tag_t;
using ::crucible::concurrent::substrate_capacity_v;

using ::crucible::concurrent::channel_byte_footprint_v;
using ::crucible::concurrent::per_call_working_set_v;

using ::crucible::concurrent::recommend_topology;
using ::crucible::concurrent::recommend_topology_for_workload;
using ::crucible::concurrent::conservative_cliff_l2_per_core;

using ::crucible::concurrent::IsSubstrate;
using ::crucible::concurrent::IsOneToOneSubstrate;
using ::crucible::concurrent::IsManyToOneSubstrate;
using ::crucible::concurrent::IsOneToManyLatestSubstrate;
using ::crucible::concurrent::IsManyToManySubstrate;
using ::crucible::concurrent::IsWorkStealingSubstrate;

using ::crucible::concurrent::Tier;
using ::crucible::concurrent::fits_in_tier_v;
using ::crucible::concurrent::required_tier_for_footprint;
using ::crucible::concurrent::substrate_required_tier_v;
using ::crucible::concurrent::substrate_hot_path_required_tier_v;
using ::crucible::concurrent::conservative_l1d_per_core;
using ::crucible::concurrent::conservative_l2_per_core;
using ::crucible::concurrent::conservative_l3_total;

using ::crucible::concurrent::SubstrateFitsCtxResidency;
using ::crucible::concurrent::StorageFitsCtxResidency;
using ::crucible::concurrent::SubstrateBenefitsFromParallelism;

}  // namespace crucible::fixy::substr

namespace crucible::fixy::substr::self_test {

static_assert(std::is_same_v<::crucible::fixy::substr::metalog::MetaLogRecord,
                             ::crucible::safety::proto::metalog_session::MetaLogRecord>,
              "fixy::substr::metalog::MetaLogRecord must alias the substrate.");

static_assert(std::is_same_v<::crucible::fixy::substr::metalog::ProducerProto,
                             ::crucible::safety::proto::metalog_session::ProducerProto>,
              "fixy::substr::metalog::ProducerProto must alias the substrate.");

static_assert(
    std::is_same_v<::crucible::fixy::substr::chainedge::Signal, ::crucible::safety::proto::chainedge_session::Signal>,
    "fixy::substr::chainedge::Signal must alias the substrate.");

static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::SignalerProto,
                             ::crucible::safety::proto::chainedge_session::SignalerProto>,
              "fixy::substr::chainedge::SignalerProto must alias the substrate.");

static_assert(std::is_same_v<::crucible::fixy::substr::spsc::ProducerProto<int>,
                             ::crucible::safety::proto::spsc_session::ProducerProto<int>>,
              "fixy::substr::spsc::ProducerProto<T> must alias the substrate.");

namespace v045 {

struct V045ProbeUserTag {};

using SpscViaFixy = ::crucible::fixy::substr::spsc::PermissionedSpscChannel<int, 64, V045ProbeUserTag>;
using SpscViaConcurrent = ::crucible::concurrent::PermissionedSpscChannel<int, 64, V045ProbeUserTag>;
static_assert(std::is_same_v<SpscViaFixy, SpscViaConcurrent>,
              "fixy::substr::spsc::PermissionedSpscChannel must alias the substrate.");

static_assert(::crucible::fixy::substr::spsc::SpscValue<int> == ::crucible::concurrent::SpscValue<int>);
static_assert(::crucible::fixy::substr::spsc::SpscValue<int>);

static_assert(std::is_same_v<::crucible::fixy::substr::spsc::spsc_tag::Whole<V045ProbeUserTag>,
                             ::crucible::concurrent::spsc_tag::Whole<V045ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::spsc::spsc_tag::Producer<V045ProbeUserTag>,
                             ::crucible::concurrent::spsc_tag::Producer<V045ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::spsc::spsc_tag::Consumer<V045ProbeUserTag>,
                             ::crucible::concurrent::spsc_tag::Consumer<V045ProbeUserTag>>);

static_assert(
    std::is_same_v<typename SpscViaFixy::whole_tag, ::crucible::fixy::substr::spsc::spsc_tag::Whole<V045ProbeUserTag>>);
static_assert(std::is_same_v<typename SpscViaFixy::producer_tag,
                             ::crucible::fixy::substr::spsc::spsc_tag::Producer<V045ProbeUserTag>>);
static_assert(std::is_same_v<typename SpscViaFixy::consumer_tag,
                             ::crucible::fixy::substr::spsc::spsc_tag::Consumer<V045ProbeUserTag>>);

static_assert(::crucible::fixy::substr::spsc::SpscChannelSessionSurface<SpscViaFixy>);

static_assert(SpscViaFixy::channel_capacity == 64);
static_assert(SpscViaFixy::channel_capacity == SpscViaConcurrent::channel_capacity);

static_assert(std::is_same_v<typename SpscViaFixy::value_type, int>);

}  // namespace v045

namespace v046 {

struct V046ProbeUserTag {};

using MpmcViaFixy = ::crucible::fixy::substr::mpmc::PermissionedMpmcChannel<int, 64, V046ProbeUserTag>;
using MpmcViaConcurrent = ::crucible::concurrent::PermissionedMpmcChannel<int, 64, V046ProbeUserTag>;
static_assert(std::is_same_v<MpmcViaFixy, MpmcViaConcurrent>,
              "fixy::substr::mpmc::PermissionedMpmcChannel must alias the substrate.");

static_assert(::crucible::fixy::substr::mpmc::MpmcValue<int> == ::crucible::concurrent::MpmcValue<int>);
static_assert(::crucible::fixy::substr::mpmc::MpmcValue<int>);

static_assert(std::is_same_v<::crucible::fixy::substr::mpmc::mpmc_tag::Whole<V046ProbeUserTag>,
                             ::crucible::concurrent::mpmc_tag::Whole<V046ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::mpmc::mpmc_tag::Producer<V046ProbeUserTag>,
                             ::crucible::concurrent::mpmc_tag::Producer<V046ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::mpmc::mpmc_tag::Consumer<V046ProbeUserTag>,
                             ::crucible::concurrent::mpmc_tag::Consumer<V046ProbeUserTag>>);

static_assert(
    std::is_same_v<typename MpmcViaFixy::whole_tag, ::crucible::fixy::substr::mpmc::mpmc_tag::Whole<V046ProbeUserTag>>);
static_assert(std::is_same_v<typename MpmcViaFixy::producer_tag,
                             ::crucible::fixy::substr::mpmc::mpmc_tag::Producer<V046ProbeUserTag>>);
static_assert(std::is_same_v<typename MpmcViaFixy::consumer_tag,
                             ::crucible::fixy::substr::mpmc::mpmc_tag::Consumer<V046ProbeUserTag>>);

static_assert(::crucible::fixy::substr::mpmc::MpmcChannelSessionSurface<MpmcViaFixy>);

static_assert(MpmcViaFixy::channel_capacity == 64);
static_assert(MpmcViaFixy::channel_capacity == MpmcViaConcurrent::channel_capacity);

static_assert(std::is_same_v<typename MpmcViaFixy::value_type, int>);

}  // namespace v046

namespace v047 {

struct V047ProbeUserTag {};

using DequeViaFixy = ::crucible::fixy::substr::chaselev::PermissionedChaseLevDeque<int, 64, V047ProbeUserTag>;
using DequeViaConcurrent = ::crucible::concurrent::PermissionedChaseLevDeque<int, 64, V047ProbeUserTag>;
static_assert(std::is_same_v<DequeViaFixy, DequeViaConcurrent>,
              "fixy::substr::chaselev::PermissionedChaseLevDeque must alias the substrate.");

static_assert(::crucible::fixy::substr::chaselev::DequeValue<int> == ::crucible::concurrent::DequeValue<int>);
static_assert(::crucible::fixy::substr::chaselev::DequeValue<int>);

static_assert(std::is_same_v<::crucible::fixy::substr::chaselev::deque_tag::Whole<V047ProbeUserTag>,
                             ::crucible::concurrent::deque_tag::Whole<V047ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::chaselev::deque_tag::Owner<V047ProbeUserTag>,
                             ::crucible::concurrent::deque_tag::Owner<V047ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::chaselev::deque_tag::Thief<V047ProbeUserTag>,
                             ::crucible::concurrent::deque_tag::Thief<V047ProbeUserTag>>);

static_assert(std::is_same_v<typename DequeViaFixy::whole_tag,
                             ::crucible::fixy::substr::chaselev::deque_tag::Whole<V047ProbeUserTag>>);
static_assert(std::is_same_v<typename DequeViaFixy::owner_tag,
                             ::crucible::fixy::substr::chaselev::deque_tag::Owner<V047ProbeUserTag>>);
static_assert(std::is_same_v<typename DequeViaFixy::thief_tag,
                             ::crucible::fixy::substr::chaselev::deque_tag::Thief<V047ProbeUserTag>>);

static_assert(::crucible::fixy::substr::chaselev::ChaseLevSessionSurface<DequeViaFixy>);

static_assert(DequeViaFixy::deque_capacity == 64);
static_assert(DequeViaFixy::deque_capacity == DequeViaConcurrent::deque_capacity);

static_assert(std::is_same_v<typename DequeViaFixy::value_type, int>);

}  // namespace v047

namespace v048 {

struct V048ProbeUserTag {};
struct V048ProbeKeyFn {
    [[nodiscard]] constexpr std::uint64_t operator()(int x) const noexcept { return static_cast<std::uint64_t>(x); }
};

using GridViaFixy = ::crucible::fixy::substr::sharded_grid::PermissionedShardedGrid<int, 4, 3, 64, V048ProbeUserTag>;
using GridViaConcurrent = ::crucible::concurrent::PermissionedShardedGrid<int, 4, 3, 64, V048ProbeUserTag,
                                                                          ::crucible::concurrent::RoundRobinRouting>;
static_assert(std::is_same_v<GridViaFixy, GridViaConcurrent>,
              "fixy::substr::sharded_grid::PermissionedShardedGrid must alias the substrate.");

using GridAffinityViaFixy = ::crucible::fixy::substr::sharded_grid::PermissionedShardedGrid<
    int, 4, 3, 64, V048ProbeUserTag, ::crucible::fixy::substr::sharded_grid::AffinityRouting>;
using GridAffinityViaConcurrent =
    ::crucible::concurrent::PermissionedShardedGrid<int, 4, 3, 64, V048ProbeUserTag,
                                                    ::crucible::concurrent::AffinityRouting>;
static_assert(std::is_same_v<GridAffinityViaFixy, GridAffinityViaConcurrent>);

static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::RoundRobinRouting,
                             ::crucible::concurrent::RoundRobinRouting>);
static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::HashKeyRouting<V048ProbeKeyFn>,
                             ::crucible::concurrent::HashKeyRouting<V048ProbeKeyFn>>);
static_assert(
    std::is_same_v<::crucible::fixy::substr::sharded_grid::AffinityRouting, ::crucible::concurrent::AffinityRouting>);

static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::grid_tag::Whole<V048ProbeUserTag>,
                             ::crucible::concurrent::grid_tag::Whole<V048ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::grid_tag::Producer<V048ProbeUserTag, 0>,
                             ::crucible::concurrent::grid_tag::Producer<V048ProbeUserTag, 0>>);
static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::grid_tag::Producer<V048ProbeUserTag, 3>,
                             ::crucible::concurrent::grid_tag::Producer<V048ProbeUserTag, 3>>);
static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::grid_tag::Consumer<V048ProbeUserTag, 0>,
                             ::crucible::concurrent::grid_tag::Consumer<V048ProbeUserTag, 0>>);
static_assert(std::is_same_v<::crucible::fixy::substr::sharded_grid::grid_tag::Consumer<V048ProbeUserTag, 2>,
                             ::crucible::concurrent::grid_tag::Consumer<V048ProbeUserTag, 2>>);

static_assert(std::is_same_v<typename GridViaFixy::whole_tag,
                             ::crucible::fixy::substr::sharded_grid::grid_tag::Whole<V048ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::user_tag, V048ProbeUserTag>);

static_assert(::crucible::fixy::substr::sharded_grid::ShardedGridSessionSurface<GridViaFixy>);
static_assert(::crucible::fixy::substr::sharded_grid::ShardedGridSessionSurface<GridAffinityViaFixy>);

static_assert(GridViaFixy::num_producers == 4);
static_assert(GridViaFixy::num_consumers == 3);
static_assert(GridViaFixy::shard_capacity == 64);
static_assert(GridViaFixy::num_producers == GridViaConcurrent::num_producers);
static_assert(GridViaFixy::num_consumers == GridViaConcurrent::num_consumers);
static_assert(GridViaFixy::shard_capacity == GridViaConcurrent::shard_capacity);

static_assert(std::is_same_v<typename GridViaFixy::value_type, int>);

}  // namespace v048

namespace v049 {

struct V049ProbeUserTag {};
struct V049ProbeKey {
    static constexpr std::uint64_t key(int v) noexcept { return static_cast<std::uint64_t>(v); }
};

using GridViaFixy =
    ::crucible::fixy::substr::sharded_calendar_grid::PermissionedShardedCalendarGrid<int, 2, 8, 16, V049ProbeKey, 1ULL,
                                                                                     V049ProbeUserTag>;
using GridViaConcurrent =
    ::crucible::concurrent::PermissionedShardedCalendarGrid<int, 2, 8, 16, V049ProbeKey, 1ULL, V049ProbeUserTag>;
static_assert(std::is_same_v<GridViaFixy, GridViaConcurrent>,
              "fixy::substr::sharded_calendar_grid::PermissionedShardedCalendarGrid "
              "must alias the substrate.");

static_assert(::crucible::fixy::substr::sharded_calendar_grid::ShardedCalendarKeyExtractorOf<V049ProbeKey, int>
              == ::crucible::concurrent::ShardedCalendarKeyExtractorOf<V049ProbeKey, int>);
static_assert(::crucible::fixy::substr::sharded_calendar_grid::ShardedCalendarKeyExtractorOf<V049ProbeKey, int>);
struct NonKeyExtractor {};
static_assert(!::crucible::fixy::substr::sharded_calendar_grid::ShardedCalendarKeyExtractorOf<NonKeyExtractor, int>);
static_assert(!::crucible::concurrent::ShardedCalendarKeyExtractorOf<NonKeyExtractor, int>);

static_assert(
    std::is_same_v<::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Whole<V049ProbeUserTag>,
                   ::crucible::concurrent::sharded_calendar_tag::Whole<V049ProbeUserTag>>);
static_assert(
    std::is_same_v<::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Producer<V049ProbeUserTag, 0>,
                   ::crucible::concurrent::sharded_calendar_tag::Producer<V049ProbeUserTag, 0>>);
static_assert(
    std::is_same_v<::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Producer<V049ProbeUserTag, 1>,
                   ::crucible::concurrent::sharded_calendar_tag::Producer<V049ProbeUserTag, 1>>);
static_assert(
    std::is_same_v<::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Consumer<V049ProbeUserTag, 0>,
                   ::crucible::concurrent::sharded_calendar_tag::Consumer<V049ProbeUserTag, 0>>);
static_assert(
    std::is_same_v<::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Consumer<V049ProbeUserTag, 1>,
                   ::crucible::concurrent::sharded_calendar_tag::Consumer<V049ProbeUserTag, 1>>);

static_assert(
    std::is_same_v<typename GridViaFixy::whole_tag,
                   ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Whole<V049ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::user_tag, V049ProbeUserTag>);
static_assert(std::is_same_v<typename GridViaFixy::key_extractor, V049ProbeKey>);
static_assert(std::is_same_v<
              typename GridViaFixy::template shard_producer_tag<0>,
              ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Producer<V049ProbeUserTag, 0>>);
static_assert(std::is_same_v<
              typename GridViaFixy::template shard_consumer_tag<1>,
              ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::Consumer<V049ProbeUserTag, 1>>);

static_assert(::crucible::fixy::substr::sharded_calendar_grid::ShardedCalendarGridSessionSurface<GridViaFixy>);

static_assert(GridViaFixy::num_shards == 2);
static_assert(GridViaFixy::num_buckets == 8);
static_assert(GridViaFixy::bucket_cap == 16);
static_assert(GridViaFixy::quantum_ns == 1ULL);
static_assert(GridViaFixy::num_shards == GridViaConcurrent::num_shards);
static_assert(GridViaFixy::num_buckets == GridViaConcurrent::num_buckets);
static_assert(GridViaFixy::bucket_cap == GridViaConcurrent::bucket_cap);
static_assert(GridViaFixy::quantum_ns == GridViaConcurrent::quantum_ns);

static_assert(std::is_same_v<typename GridViaFixy::value_type, int>);

}  // namespace v049

namespace v050 {

struct V050ProbeUserTag {};
struct V050ProbeKey {
    static std::uint64_t key(int v) noexcept { return static_cast<std::uint64_t>(v); }
};

using GridViaFixy = ::crucible::fixy::substr::calendar_grid::PermissionedCalendarGrid<int, 4, 8, 16, V050ProbeKey, 1ULL,
                                                                                      V050ProbeUserTag>;
using GridViaConcurrent =
    ::crucible::concurrent::PermissionedCalendarGrid<int, 4, 8, 16, V050ProbeKey, 1ULL, V050ProbeUserTag>;
static_assert(std::is_same_v<GridViaFixy, GridViaConcurrent>, "fixy::substr::calendar_grid::PermissionedCalendarGrid "
                                                              "must alias the substrate.");

static_assert(::crucible::fixy::substr::calendar_grid::KeyExtractorOf<V050ProbeKey, int>
              == ::crucible::concurrent::KeyExtractorOf<V050ProbeKey, int>);
static_assert(::crucible::fixy::substr::calendar_grid::KeyExtractorOf<V050ProbeKey, int>);
struct NonCalendarKey {};
static_assert(!::crucible::fixy::substr::calendar_grid::KeyExtractorOf<NonCalendarKey, int>);
static_assert(!::crucible::concurrent::KeyExtractorOf<NonCalendarKey, int>);

static_assert(std::is_same_v<::crucible::fixy::substr::calendar_grid::calendar_tag::Whole<V050ProbeUserTag>,
                             ::crucible::concurrent::calendar_tag::Whole<V050ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<V050ProbeUserTag, 0>,
                             ::crucible::concurrent::calendar_tag::Producer<V050ProbeUserTag, 0>>);
static_assert(std::is_same_v<::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<V050ProbeUserTag, 3>,
                             ::crucible::concurrent::calendar_tag::Producer<V050ProbeUserTag, 3>>);
static_assert(std::is_same_v<::crucible::fixy::substr::calendar_grid::calendar_tag::Consumer<V050ProbeUserTag>,
                             ::crucible::concurrent::calendar_tag::Consumer<V050ProbeUserTag>>);

static_assert(std::is_same_v<typename GridViaFixy::whole_tag,
                             ::crucible::fixy::substr::calendar_grid::calendar_tag::Whole<V050ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::consumer_tag,
                             ::crucible::fixy::substr::calendar_grid::calendar_tag::Consumer<V050ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::user_tag, V050ProbeUserTag>);
static_assert(std::is_same_v<typename GridViaFixy::key_extractor, V050ProbeKey>);
static_assert(std::is_same_v<typename GridViaFixy::template producer_tag<0>,
                             ::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<V050ProbeUserTag, 0>>);
static_assert(std::is_same_v<typename GridViaFixy::template producer_tag<3>,
                             ::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<V050ProbeUserTag, 3>>);

static_assert(::crucible::fixy::substr::calendar_grid::CalendarGridSessionSurface<GridViaFixy>);

static_assert(GridViaFixy::num_producers == 4);
static_assert(GridViaFixy::num_buckets == 8);
static_assert(GridViaFixy::bucket_cap == 16);
static_assert(GridViaFixy::quantum_ns == 1ULL);
static_assert(GridViaFixy::num_producers == GridViaConcurrent::num_producers);
static_assert(GridViaFixy::num_buckets == GridViaConcurrent::num_buckets);
static_assert(GridViaFixy::bucket_cap == GridViaConcurrent::bucket_cap);
static_assert(GridViaFixy::quantum_ns == GridViaConcurrent::quantum_ns);

static_assert(std::is_same_v<typename GridViaFixy::value_type, int>);

}  // namespace v050

namespace v051 {

struct V051ProbeUserTag {};

using LogViaFixy = ::crucible::fixy::substr::metalog::PermissionedMetaLog<V051ProbeUserTag>;
using LogViaConcurrent = ::crucible::concurrent::PermissionedMetaLog<V051ProbeUserTag>;
static_assert(std::is_same_v<LogViaFixy, LogViaConcurrent>,
              "fixy::substr::metalog::PermissionedMetaLog must alias the substrate.");

static_assert(std::is_same_v<::crucible::fixy::substr::metalog::MetaIndex, ::crucible::MetaIndex>,
              "fixy::substr::metalog::MetaIndex must alias ::crucible::MetaIndex.");

static_assert(std::is_same_v<::crucible::fixy::substr::metalog::metalog_tag::Whole<V051ProbeUserTag>,
                             ::crucible::concurrent::metalog_tag::Whole<V051ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::metalog::metalog_tag::Producer<V051ProbeUserTag>,
                             ::crucible::concurrent::metalog_tag::Producer<V051ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::metalog::metalog_tag::Consumer<V051ProbeUserTag>,
                             ::crucible::concurrent::metalog_tag::Consumer<V051ProbeUserTag>>);

static_assert(std::is_same_v<typename LogViaFixy::whole_tag,
                             ::crucible::fixy::substr::metalog::metalog_tag::Whole<V051ProbeUserTag>>);
static_assert(std::is_same_v<typename LogViaFixy::producer_tag,
                             ::crucible::fixy::substr::metalog::metalog_tag::Producer<V051ProbeUserTag>>);
static_assert(std::is_same_v<typename LogViaFixy::consumer_tag,
                             ::crucible::fixy::substr::metalog::metalog_tag::Consumer<V051ProbeUserTag>>);

static_assert(::crucible::fixy::substr::metalog::MetaLogSessionSurface<LogViaFixy>);

static_assert(std::is_same_v<typename LogViaFixy::value_type, ::crucible::TensorMeta>);
static_assert(std::is_same_v<typename LogViaFixy::value_type, ::crucible::fixy::substr::metalog::MetaLogRecord>);

static_assert(std::is_same_v<::crucible::fixy::substr::metalog::ProducerProto,
                             ::crucible::safety::proto::metalog_session::ProducerProto>);
static_assert(std::is_same_v<::crucible::fixy::substr::metalog::ConsumerProto,
                             ::crucible::safety::proto::metalog_session::ConsumerProto>);

}  // namespace v051

namespace v052 {

struct V052ProbeUserTag {};

using EdgeViaFixy =
    ::crucible::fixy::substr::chainedge::PermissionedChainEdge<::crucible::concurrent::VendorBackend::CPU,
                                                               V052ProbeUserTag>;
using EdgeViaConcurrent =
    ::crucible::concurrent::PermissionedChainEdge<::crucible::concurrent::VendorBackend::CPU, V052ProbeUserTag>;
static_assert(std::is_same_v<EdgeViaFixy, EdgeViaConcurrent>,
              "fixy::substr::chainedge::PermissionedChainEdge must alias the substrate.");

using EdgeNvViaFixy =
    ::crucible::fixy::substr::chainedge::PermissionedChainEdge<::crucible::fixy::substr::chainedge::VendorBackend::NV,
                                                               V052ProbeUserTag>;
using EdgeNvViaConcurrent =
    ::crucible::concurrent::PermissionedChainEdge<::crucible::concurrent::VendorBackend::NV, V052ProbeUserTag>;
static_assert(std::is_same_v<EdgeNvViaFixy, EdgeNvViaConcurrent>,
              "fixy::substr::chainedge::PermissionedChainEdge<NV> must alias the "
              "non-default-backend substrate variant.");

static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::VendorBackend, ::crucible::concurrent::VendorBackend>,
              "fixy::substr::chainedge::VendorBackend must alias "
              "::crucible::concurrent::VendorBackend.");
static_assert(static_cast<int>(::crucible::fixy::substr::chainedge::VendorBackend::CPU)
              == static_cast<int>(::crucible::concurrent::VendorBackend::CPU));
static_assert(static_cast<int>(::crucible::fixy::substr::chainedge::VendorBackend::NV)
              == static_cast<int>(::crucible::concurrent::VendorBackend::NV));

static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::chainedge_tag::Whole<V052ProbeUserTag>,
                             ::crucible::concurrent::chainedge_tag::Whole<V052ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::chainedge_tag::Signaler<V052ProbeUserTag>,
                             ::crucible::concurrent::chainedge_tag::Signaler<V052ProbeUserTag>>);
static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::chainedge_tag::Waiter<V052ProbeUserTag>,
                             ::crucible::concurrent::chainedge_tag::Waiter<V052ProbeUserTag>>);

static_assert(std::is_same_v<typename EdgeViaFixy::whole_tag,
                             ::crucible::fixy::substr::chainedge::chainedge_tag::Whole<V052ProbeUserTag>>);
static_assert(std::is_same_v<typename EdgeViaFixy::signaler_tag,
                             ::crucible::fixy::substr::chainedge::chainedge_tag::Signaler<V052ProbeUserTag>>);
static_assert(std::is_same_v<typename EdgeViaFixy::waiter_tag,
                             ::crucible::fixy::substr::chainedge::chainedge_tag::Waiter<V052ProbeUserTag>>);

static_assert(::crucible::fixy::substr::chainedge::ChainEdgeSessionSurface<EdgeViaFixy>);

static_assert(std::is_same_v<typename EdgeViaFixy::value_type, ::crucible::concurrent::SemaphoreSignal>);
static_assert(std::is_same_v<typename EdgeViaFixy::value_type, ::crucible::fixy::substr::chainedge::Signal>);

static_assert(EdgeViaFixy::backend == ::crucible::concurrent::VendorBackend::CPU);
static_assert(EdgeNvViaFixy::backend == ::crucible::concurrent::VendorBackend::NV);

static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::SignalerProto,
                             ::crucible::safety::proto::chainedge_session::SignalerProto>);
static_assert(std::is_same_v<::crucible::fixy::substr::chainedge::WaiterProto,
                             ::crucible::safety::proto::chainedge_session::WaiterProto>);

}  // namespace v052

// These counts track using-declarations only.  An alias template or a
// nested tag namespace adds a name without a using-declaration, so
// neither moves a count.  `mpsc::` contributes nothing at all, because
// it defines its mints instead of a re-export.
constexpr int substr_spsc_using = 3;
constexpr int substr_swmr_using = 6;
constexpr int substr_chaselev_using = 5;
constexpr int substr_metalog_using = 5;
constexpr int substr_chainedge_using = 5;
constexpr int substr_mpmc_using = 5;
constexpr int substr_calendar_grid_using = 5;
constexpr int substr_sharded_calendar_grid_using = 5;
constexpr int substr_sharded_grid_using = 7;
constexpr int substr_snapshot_using = 4;
constexpr int substr_outer_using = 1;

constexpr int substr_total_using = substr_spsc_using + substr_swmr_using + substr_chaselev_using + substr_metalog_using
                                 + substr_chainedge_using + substr_mpmc_using + substr_calendar_grid_using
                                 + substr_sharded_calendar_grid_using + substr_sharded_grid_using
                                 + substr_snapshot_using + substr_outer_using;

static_assert(substr_total_using == 51, "fixy::substr:: using-decl surface drifted from 51 — the "
                                        "sub-namespace re-exports and this sentinel must update in lockstep.");

// A concept has no type to compare.  These checks pin the two paths
// against each other on the same inputs instead.  The items that do
// have a type get a direct comparison.

namespace u051 {

struct U051ProbeUserTag {};

namespace cc = ::crucible::concurrent;
namespace eff = ::crucible::effects;

static_assert(static_cast<int>(ChannelTopology::OneToOne) == static_cast<int>(cc::ChannelTopology::OneToOne));
static_assert(static_cast<int>(ChannelTopology::WorkStealing) == static_cast<int>(cc::ChannelTopology::WorkStealing));

using SpscViaFixy = Substrate_t<ChannelTopology::OneToOne, int, 1024, U051ProbeUserTag>;
using SpscViaSubstrate = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 1024, U051ProbeUserTag>;
static_assert(std::is_same_v<SpscViaFixy, SpscViaSubstrate>,
              "fixy::substr::Substrate_t must alias concurrent::Substrate_t");

using SnapViaFixy = Substrate_t<ChannelTopology::OneToMany_Latest, double, 0, U051ProbeUserTag>;
using SnapViaSubstrate = cc::Substrate_t<cc::ChannelTopology::OneToMany_Latest, double, 0, U051ProbeUserTag>;
static_assert(std::is_same_v<SnapViaFixy, SnapViaSubstrate>);

static_assert(substrate_topology_v<SpscViaFixy> == cc::substrate_topology_v<SpscViaSubstrate>);
static_assert(substrate_capacity_v<SpscViaFixy> == cc::substrate_capacity_v<SpscViaSubstrate>);
static_assert(std::is_same_v<substrate_value_type_t<SpscViaFixy>, cc::substrate_value_type_t<SpscViaSubstrate>>);
static_assert(std::is_same_v<substrate_user_tag_t<SpscViaFixy>, cc::substrate_user_tag_t<SpscViaSubstrate>>);

static_assert(channel_byte_footprint_v<SpscViaFixy> == cc::channel_byte_footprint_v<SpscViaSubstrate>);
static_assert(per_call_working_set_v<SpscViaFixy> == cc::per_call_working_set_v<SpscViaSubstrate>);

static_assert(IsSubstrate<SpscViaFixy>);
static_assert(IsSubstrate<SnapViaFixy>);
static_assert(IsOneToOneSubstrate<SpscViaFixy>);
static_assert(!IsOneToOneSubstrate<SnapViaFixy>);
static_assert(IsOneToManyLatestSubstrate<SnapViaFixy>);
static_assert(!IsManyToManySubstrate<SpscViaFixy>);
static_assert(!IsWorkStealingSubstrate<SpscViaFixy>);
static_assert(!IsManyToOneSubstrate<SpscViaFixy>);

static_assert(IsSubstrate<SpscViaFixy> == cc::IsSubstrate<SpscViaSubstrate>);
static_assert(IsOneToOneSubstrate<SpscViaFixy> == cc::IsOneToOneSubstrate<SpscViaSubstrate>);

static_assert(static_cast<int>(Tier::L1Resident) == static_cast<int>(cc::Tier::L1Resident));
static_assert(static_cast<int>(Tier::DRAMBound) == static_cast<int>(cc::Tier::DRAMBound));

static_assert(conservative_l1d_per_core == cc::conservative_l1d_per_core);
static_assert(conservative_l2_per_core == cc::conservative_l2_per_core);
static_assert(conservative_l3_total == cc::conservative_l3_total);
static_assert(conservative_cliff_l2_per_core == cc::conservative_cliff_l2_per_core);

static_assert(fits_in_tier_v<4 * 1024, Tier::L1Resident> == cc::fits_in_tier_v<4 * 1024, cc::Tier::L1Resident>);
static_assert(fits_in_tier_v<4 * 1024, Tier::L1Resident>);
static_assert(!fits_in_tier_v<256 * 1024, Tier::L1Resident>);

static_assert(required_tier_for_footprint<sizeof(double)> == cc::required_tier_for_footprint<sizeof(double)>);
static_assert(required_tier_for_footprint<sizeof(double)> == Tier::L1Resident);

static_assert(substrate_required_tier_v<SpscViaFixy> == cc::substrate_required_tier_v<SpscViaSubstrate>);
static_assert(substrate_hot_path_required_tier_v<SpscViaFixy>
              == cc::substrate_hot_path_required_tier_v<SpscViaSubstrate>);
static_assert(substrate_hot_path_required_tier_v<SpscViaFixy> == Tier::L1Resident);

// The per-call working set of a small channel fits the first-level
// cache.  The residency gate holds for the hottest context as well as
// for the colder ones.
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::HotFgCtx>
              == cc::SubstrateFitsCtxResidency<SpscViaSubstrate, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::BgDrainCtx>);
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::ColdInitCtx>);

// The same channel fits that cache by total storage, not only per call.
static_assert(StorageFitsCtxResidency<SpscViaFixy, eff::HotFgCtx>
              == cc::StorageFitsCtxResidency<SpscViaSubstrate, eff::HotFgCtx>);
static_assert(StorageFitsCtxResidency<SpscViaFixy, eff::HotFgCtx>);

// It also sits below the parallelism cliff, so that gate is false.
static_assert(SubstrateBenefitsFromParallelism<SpscViaFixy> == cc::SubstrateBenefitsFromParallelism<SpscViaSubstrate>);
static_assert(!SubstrateBenefitsFromParallelism<SpscViaFixy>);

// A channel a thousand times larger crosses the cliff.
using LargeSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024 * 1024, U051ProbeUserTag>;
static_assert(SubstrateBenefitsFromParallelism<LargeSpsc>);
// Its per-call working set is still counters and one cell, so the
// residency gate stays true.
static_assert(SubstrateFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);

static_assert(recommend_topology(1, 1) == ChannelTopology::OneToOne);
static_assert(recommend_topology(4, 1) == ChannelTopology::ManyToOne);
static_assert(recommend_topology(4, 4) == ChannelTopology::ManyToMany);
static_assert(recommend_topology(1, 4, /*latest_only=*/true) == ChannelTopology::OneToMany_Latest);
static_assert(recommend_topology_for_workload(1, 1, 4 * 1024) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(4, 4, 4 * 1024 * 1024) == ChannelTopology::ManyToMany);

constexpr int u051_surface_cardinality = 32;
static_assert(u051_surface_cardinality == 32, "the topology and ctx-fit using-decl surface drifted from 32 — "
                                              "that block and this sentinel must update in lockstep.");

}  // namespace u051

}  // namespace crucible::fixy::substr::self_test
