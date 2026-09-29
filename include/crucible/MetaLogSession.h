#pragma once

// The typed-session facade over PermissionedMetaLog: a foreground producer
// appends TensorMeta records and a background consumer drains them.  Each
// side is a local session over its own channel handle, with the empty
// permission set.
//
// The session owns the channel handle.  mint_metalog_producer_session and
// mint_metalog_consumer_session take the handle by move and give it back
// when the session reaches End.  A caller that runs the whole session in
// one scope lends the handle instead, with
//     fixy::session::with_session<ProducerProto>(ctx, std::move(handle), body)
// which gives the handle back after the body closes the session.  So no
// session holds a pointer to a channel handle that can move or die first,
// and the caller never acts on the log beside the session.
//
// Each protocol loops and ends by a local choice: the producer picks when
// to stop appending, and the consumer picks when to stop draining.  The
// two sides are not one channel.  The log carries records, not labels, so
// each handle states the Local network, and each side picks its branch
// with select<I>(fixy::session::no_label).
//
// A transport of this facade tries once and reports whether it took or
// found a record.  When the log is full, or holds nothing, the handle
// waits through the watch of fixy/session/Watch.h and tries again.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::metalog_session {

using MetaLogRecord = ::crucible::TensorMeta;

using ProducerProto = ::fixy::session::Loop<
    ::fixy::session::Select<::fixy::session::Send<MetaLogRecord, ::fixy::session::Continue>, ::fixy::session::End>>;
using ConsumerProto = ::fixy::session::Loop<
    ::fixy::session::Select<::fixy::session::Recv<MetaLogRecord, ::fixy::session::Continue>, ::fixy::session::End>>;

// The shape the facade needs from a permissioned log: the three tags, the
// two role-typed handles, and the operations the transports call.
template <typename Log>
concept MetaLogSessionSurface =
    requires(Log& log, typename Log::ProducerHandle& producer, typename Log::ConsumerHandle& consumer,
             ::foundation::permissions::Permission<typename Log::producer_tag> prod_perm,
             ::foundation::permissions::Permission<typename Log::consumer_tag> cons_perm, const MetaLogRecord* records,
             const MetaLogRecord& record) {
        typename Log::value_type;
        typename Log::producer_tag;
        typename Log::consumer_tag;
        typename Log::ProducerHandle;
        typename Log::ConsumerHandle;

        requires std::same_as<typename Log::value_type, MetaLogRecord>;
        requires std::same_as<typename Log::ProducerHandle::value_type, MetaLogRecord>;
        requires std::same_as<typename Log::ConsumerHandle::value_type, MetaLogRecord>;

        { log.producer(std::move(prod_perm)) } -> std::same_as<typename Log::ProducerHandle>;
        { log.consumer(std::move(cons_perm)) } -> std::same_as<typename Log::ConsumerHandle>;
        { producer.try_append(records, std::uint32_t{1}) } -> std::same_as<::crucible::MetaIndex>;
        { producer.try_append_one(record) } -> std::same_as<bool>;
        { consumer.try_drain_one() } -> std::same_as<std::optional<MetaLogRecord>>;
    };

// The trying write of the producer: true when the log took the record.
inline constexpr auto append_one = [](auto& producer, MetaLogRecord& record) -> bool {
    return producer.try_append_one(record);
};

// The polling read of the consumer: the next record, or none while the log
// holds nothing.
inline constexpr auto drain_one = [](auto& consumer) -> std::optional<MetaLogRecord> {
    return consumer.try_drain_one();
};

// The gate of each mint: a log of the right shape, the handle of the
// mint's role passed by move, and the gate of fixy::session::mint_session
// for the role's protocol over that handle.  The handle is a template
// parameter so that each refusal is a clause of this one concept: a type
// that is not a permissioned log, a handle of the other role, and a handle
// passed as an lvalue all fail here.
template <typename Ctx, typename Log, typename Handle>
concept CtxFitsMetaLogProducerSession = MetaLogSessionSurface<Log> && std::same_as<Handle, typename Log::ProducerHandle>
                                     && ::fixy::session::CtxFitsSession<Ctx, ProducerProto, Handle>;

template <typename Ctx, typename Log, typename Handle>
concept CtxFitsMetaLogConsumerSession = MetaLogSessionSurface<Log> && std::same_as<Handle, typename Log::ConsumerHandle>
                                     && ::fixy::session::CtxFitsSession<Ctx, ConsumerProto, Handle>;

template <typename Log, typename Ctx, typename Handle>
    requires CtxFitsMetaLogProducerSession<Ctx, Log, Handle>
[[nodiscard]] constexpr auto mint_metalog_producer_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::mint_session<ProducerProto>(ctx, std::forward<Handle>(handle));
}

template <typename Log, typename Ctx, typename Handle>
    requires CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>
[[nodiscard]] constexpr auto mint_metalog_consumer_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::mint_session<ConsumerProto>(ctx, std::forward<Handle>(handle));
}

template <MetaLogSessionSurface Log, typename Ctx>
using ProducerSessionHandle = decltype(mint_metalog_producer_session<Log>(
    std::declval<Ctx const&>(), std::declval<typename Log::ProducerHandle>()));

template <MetaLogSessionSurface Log, typename Ctx>
using ConsumerSessionHandle = decltype(mint_metalog_consumer_session<Log>(
    std::declval<Ctx const&>(), std::declval<typename Log::ConsumerHandle>()));

// The gate cases that need an execution context are in
// test/test_metalog_session.cpp, because a production header has no
// context to name.
namespace detail::metalog_session_self_test {

struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;

static_assert(MetaLogSessionSurface<Log>);
static_assert(!MetaLogSessionSurface<int>, "an int is not a permissioned log");

static_assert(sizeof(Log::ProducerHandle) == sizeof(::crucible::MetaLog*),
              "metalog_session: ProducerHandle must stay pointer-sized; the Permission token collapses through EBO.");
static_assert(sizeof(Log::ConsumerHandle) == sizeof(::crucible::MetaLog*),
              "metalog_session: ConsumerHandle must stay pointer-sized; the Permission token collapses through EBO.");

static_assert(::fixy::session::AdmitsLocalChoice<Log::ProducerHandle, ProducerProto, void>);
static_assert(::fixy::session::AdmitsLocalChoice<Log::ConsumerHandle, ConsumerProto, void>);

}  // namespace detail::metalog_session_self_test

}  // namespace crucible::metalog_session
