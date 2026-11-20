#pragma once

// The typed-session facade over PermissionedMetaLog: a foreground producer
// appends TensorMeta records and a background consumer drains them.  Each
// side is a local session over its own channel handle, with the empty
// permission set.
//
// The session owns the channel handle.  mint_metalog_producer_session and
// mint_metalog_consumer_session take the handle by move and give it back
// when the session reaches End.  So no session holds a pointer to a channel
// handle that can move or die first, and the caller never acts on the log
// beside the session.
//
// Each handle binds the log, so it is a pole of a channel
// (fixy/session/Admission.h), and it opens only through the pole door on
// the protocol that the log table of fixy/channels/LocalLogSession.h names.
// The producer runs Loop<Select<Send<TensorMeta, Continue>, End>>, and the
// consumer runs Loop<Select<Recv<TensorMeta, Continue>, End>>.  Each
// protocol loops and ends by a local choice: the producer picks when to stop
// appending, and the consumer picks when to stop draining.  The log carries
// records, not labels, so each handle states the Local network, each side
// picks its branch with select<I>(fixy::session::no_label), and the relation
// of a local choice of that header holds for the two protocols.
//
// Each step takes the context of the step and no transport.  The log table
// of fixy/channels/LocalLogSession.h appends with try_append_one_pure in
// that context and drains with try_drain_one, each one time.  When the log
// is full, or holds nothing, the handle waits through the watch of
// fixy/session/Watch.h and tries again.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/Core.h>
#include <fixy/channels/LocalLogSession.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::metalog_session {

using MetaLogRecord = ::crucible::TensorMeta;

// The shape of a permissioned log that the facade uses: the brand, the
// three tags, the two role-typed handles, and the operations that the log
// table calls.
template <typename Log>
concept MetaLogSessionSurface =
    requires(Log& log, typename Log::ProducerHandle& producer, typename Log::ConsumerHandle& consumer,
             ::foundation::permissions::Permission<typename Log::producer_tag, typename Log::brand_type> prod_perm,
             ::foundation::permissions::Permission<typename Log::consumer_tag, typename Log::brand_type> cons_perm,
             ::fixy::View<const MetaLogRecord> records, const MetaLogRecord& record,
             ::foundation::effects::ExecCtx<> const& pure) {
        typename Log::value_type;
        typename Log::brand_type;
        typename Log::producer_tag;
        typename Log::consumer_tag;
        typename Log::ProducerHandle;
        typename Log::ConsumerHandle;

        requires std::same_as<typename Log::value_type, MetaLogRecord>;
        requires std::same_as<typename Log::ProducerHandle::value_type, MetaLogRecord>;
        requires std::same_as<typename Log::ConsumerHandle::value_type, MetaLogRecord>;

        { log.producer(std::move(prod_perm)) } -> std::same_as<typename Log::ProducerHandle>;
        { log.consumer(std::move(cons_perm)) } -> std::same_as<typename Log::ConsumerHandle>;
        { producer.try_append_pure(pure, records) } -> std::same_as<::crucible::MetaIndex>;
        { producer.try_append_one_pure(pure, record) } -> std::same_as<bool>;
        { consumer.try_drain_one() } -> std::same_as<::fixy::Option<MetaLogRecord>>;
    };

// The gate of each mint: a log of the right shape, the handle of the
// mint's role passed by move, and the gate of the pole door of
// fixy/session/Handle.h for that handle.  The handle is a template
// parameter so that each refusal is a clause of this one concept: a type
// that is not a permissioned log, a handle of the other role, and a handle
// passed as an lvalue all fail here.
template <typename Ctx, typename Log, typename Handle>
concept CtxFitsMetaLogProducerSession = MetaLogSessionSurface<Log> && std::same_as<Handle, typename Log::ProducerHandle>
                                     && ::fixy::session::CtxFitsPoleSession<Ctx, Handle>;

template <typename Ctx, typename Log, typename Handle>
concept CtxFitsMetaLogConsumerSession = MetaLogSessionSurface<Log> && std::same_as<Handle, typename Log::ConsumerHandle>
                                     && ::fixy::session::CtxFitsPoleSession<Ctx, Handle>;

template <typename Log, typename Ctx, typename Handle>
    requires CtxFitsMetaLogProducerSession<Ctx, Log, Handle>
[[nodiscard]] constexpr auto mint_metalog_producer_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::SessionMintDoor::open_pole<::fixy::session::DefaultAbandonmentPolicy>(
        ctx, std::forward<Handle>(handle));
}

template <typename Log, typename Ctx, typename Handle>
    requires CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>
[[nodiscard]] constexpr auto mint_metalog_consumer_session(Ctx const& ctx, Handle&& handle) noexcept {
    return ::fixy::session::SessionMintDoor::open_pole<::fixy::session::DefaultAbandonmentPolicy>(
        ctx, std::forward<Handle>(handle));
}

template <MetaLogSessionSurface Log, typename Ctx>
using ProducerSessionHandle = decltype(mint_metalog_producer_session<Log>(
    std::declval<Ctx const&>(), std::declval<typename Log::ProducerHandle>()));

template <MetaLogSessionSurface Log, typename Ctx>
using ConsumerSessionHandle = decltype(mint_metalog_consumer_session<Log>(
    std::declval<Ctx const&>(), std::declval<typename Log::ConsumerHandle>()));

}  // namespace crucible::metalog_session
