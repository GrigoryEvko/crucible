#pragma once

// MetaLogSession.h — typed-session facade for PermissionedMetaLog.
//
// MetaLog's production shape is an infinite foreground-to-background
// stream of TensorMeta records.  PermissionedMetaLog enforces the raw
// role split; this header adds the protocol shape:
//
//   Producer: Loop<Send<TensorMeta, Continue>>
//   Consumer: Loop<Recv<TensorMeta, Continue>>
//
// EmptyPermSet is deliberate.  Producer/consumer authority stays in
// the endpoint handles; TensorMeta payloads do not transfer permissions.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>

#include <concepts>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::metalog_session {

using MetaLogRecord = ::crucible::TensorMeta;

using ProducerProto = Loop<Send<MetaLogRecord, Continue>>;
using ConsumerProto = Loop<Recv<MetaLogRecord, Continue>>;

template <typename Log>
concept MetaLogSessionSurface = requires(
    Log& log,
    typename Log::ProducerHandle& producer,
    typename Log::ConsumerHandle& consumer,
    ::crucible::safety::Permission<typename Log::producer_tag> prod_perm,
    ::crucible::safety::Permission<typename Log::consumer_tag> cons_perm,
    const MetaLogRecord* records,
    const MetaLogRecord& record)
{
    typename Log::value_type;
    typename Log::producer_tag;
    typename Log::consumer_tag;
    typename Log::ProducerHandle;
    typename Log::ConsumerHandle;

    requires std::same_as<typename Log::value_type, MetaLogRecord>;
    requires std::same_as<typename Log::ProducerHandle::value_type, MetaLogRecord>;
    requires std::same_as<typename Log::ConsumerHandle::value_type, MetaLogRecord>;

    { log.producer(std::move(prod_perm)) }
        -> std::same_as<typename Log::ProducerHandle>;
    { log.consumer(std::move(cons_perm)) }
        -> std::same_as<typename Log::ConsumerHandle>;
    { producer.try_append(records, std::uint32_t{1}) }
        -> std::same_as<::crucible::MetaIndex>;
    { producer.try_append_one(record) }
        -> std::same_as<bool>;
    { consumer.try_drain_one() }
        -> std::same_as<std::optional<MetaLogRecord>>;
};

template <MetaLogSessionSurface Log>
[[nodiscard]] auto mint_metalog_producer(
    Log& log,
    ::crucible::safety::Permission<typename Log::producer_tag>&& perm) noexcept
{
    return log.producer(std::move(perm));
}

template <MetaLogSessionSurface Log>
[[nodiscard]] auto mint_metalog_consumer(
    Log& log,
    ::crucible::safety::Permission<typename Log::consumer_tag>&& perm) noexcept
{
    return log.consumer(std::move(perm));
}

template <MetaLogSessionSurface Log>
[[nodiscard]] constexpr auto
mint_metalog_producer_session(typename Log::ProducerHandle& handle) noexcept
{
    return mint_permissioned_session<ProducerProto,
                                     typename Log::ProducerHandle*>(&handle);
}

template <MetaLogSessionSurface Log>
[[nodiscard]] constexpr auto
mint_metalog_consumer_session(typename Log::ConsumerHandle& handle) noexcept
{
    return mint_permissioned_session<ConsumerProto,
                                     typename Log::ConsumerHandle*>(&handle);
}

template <MetaLogSessionSurface Log>
using ProducerSessionHandle = decltype(
    mint_metalog_producer_session<Log>(
        std::declval<typename Log::ProducerHandle&>()));

template <MetaLogSessionSurface Log>
using ConsumerSessionHandle = decltype(
    mint_metalog_consumer_session<Log>(
        std::declval<typename Log::ConsumerHandle&>()));

inline constexpr auto blocking_append = [](auto& hp, const MetaLogRecord& record) {
    while (!hp->try_append_one(record)) {
        std::this_thread::yield();
    }
};

inline constexpr auto blocking_drain = [](auto& hp) {
    for (;;) {
        if (auto record = hp->try_drain_one()) {
            return *record;
        }
        std::this_thread::yield();
    }
};

namespace detail::metalog_session_self_test {

struct Tag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<Tag>;
using ProducerHandle = Log::ProducerHandle;
using ConsumerHandle = Log::ConsumerHandle;
using ProducerSession = ProducerSessionHandle<Log>;
using ConsumerSession = ConsumerSessionHandle<Log>;

static_assert(MetaLogSessionSurface<Log>);
static_assert(std::is_same_v<ProducerProto,
                             Loop<Send<MetaLogRecord, Continue>>>);
static_assert(std::is_same_v<ConsumerProto,
                             Loop<Recv<MetaLogRecord, Continue>>>);
static_assert(std::is_same_v<typename ProducerSession::protocol,
                             Send<MetaLogRecord, Continue>>);
static_assert(std::is_same_v<typename ConsumerSession::protocol,
                             Recv<MetaLogRecord, Continue>>);
static_assert(std::is_same_v<typename ProducerSession::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<typename ConsumerSession::perm_set, EmptyPermSet>);

static_assert(sizeof(ProducerHandle) == sizeof(::crucible::MetaLog*),
              "metalog_session: ProducerHandle must stay pointer-sized; "
              "the Permission token should collapse through EBO.");
static_assert(sizeof(ConsumerHandle) == sizeof(::crucible::MetaLog*),
              "metalog_session: ConsumerHandle must stay pointer-sized; "
              "the Permission token should collapse through EBO.");
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ProducerHandle*>)
              == sizeof(SessionHandle<End, ProducerHandle*>),
              "metalog_session: producer PSH pointer resource must remain "
              "same size as bare SessionHandle.");
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ConsumerHandle*>)
              == sizeof(SessionHandle<End, ConsumerHandle*>),
              "metalog_session: consumer PSH pointer resource must remain "
              "same size as bare SessionHandle.");
static_assert(sizeof(ProducerSession)
              == sizeof(SessionHandle<typename ProducerSession::protocol,
                                      ProducerHandle*,
                                      typename ProducerSession::loop_ctx>),
              "metalog_session: actual minted producer loop-head PSH must "
              "remain the same size as the bare session handle.");
static_assert(sizeof(ConsumerSession)
              == sizeof(SessionHandle<typename ConsumerSession::protocol,
                                      ConsumerHandle*,
                                      typename ConsumerSession::loop_ctx>),
              "metalog_session: actual minted consumer loop-head PSH must "
              "remain the same size as the bare session handle.");

}  // namespace detail::metalog_session_self_test

}  // namespace crucible::safety::proto::metalog_session
