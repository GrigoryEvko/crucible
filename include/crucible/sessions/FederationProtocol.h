#pragma once

// The byte-stable codec lives elsewhere. This header fixes only the legal
// message order around it.
//
// The projected sender and receiver views are the production-facing handles.
// The coordinator role is part of the global type so that the protocol is one
// honest three-party description rather than two unrelated binary ones.

#include <crucible/Types.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/diag/RowMismatch.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionMint.h>

#include <span>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::federation {

// Every send and receive on a role protocol is a network round trip that waits
// on a remote peer. The row is IO because the channel touches kernel sockets and
// device queues, and Block because the two operations wait synchronously.
//
// A foreground context permits the empty row and can never widen to IO or Block,
// so the gate below is unsatisfiable there. A background context whose permitted
// row already covers both widens to them and mints.

using federation_required_row =
    ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;

// Never called. Its address is passed to the row-mismatch assertion so the
// diagnostic carries a name the reader can search for.
[[noreturn]] inline void federation_mint_boundary() noexcept { std::abort(); }

template <typename Ctx>
concept CtxFitsFederation =
    ::crucible::effects::IsExecCtx<Ctx> && ::crucible::effects::Subrow<federation_required_row, typename Ctx::row_type>;

struct SenderRole {};
struct ReceiverRole {};
struct CoordRole {};

struct AnyFederationKey {};

template <typename KeyTag = AnyFederationKey>
struct Ack {
    using key_tag = KeyTag;
    ::crucible::KernelCacheKey key{};
};

template <typename KeyTag = AnyFederationKey>
struct PullRequest {
    using key_tag = KeyTag;
    ::crucible::KernelCacheKey key{};
};

template <typename KeyTag = AnyFederationKey>
struct FederationEntryPayload {
    using key_tag = KeyTag;
    std::span<const std::uint8_t> bytes{};
};

template <typename KeyTag = AnyFederationKey>
using HeaderPayload = ContentAddressed<::crucible::cipher::federation::FederationEntryHeader>;

template <typename KeyTag = AnyFederationKey>
using BodyPayload = ContentAddressed<FederationEntryPayload<KeyTag>>;

template <typename KeyTag = AnyFederationKey>
using FederationGlobal =
    Rec_G<Transmission<SenderRole, CoordRole, HeaderPayload<KeyTag>,
                       Transmission<CoordRole, SenderRole, Ack<KeyTag>,
                                    Transmission<CoordRole, ReceiverRole, PullRequest<KeyTag>,
                                                 Transmission<ReceiverRole, CoordRole, BodyPayload<KeyTag>, Var_G>>>>>;

using FederationProtocol = FederationGlobal<AnyFederationKey>;

template <typename KeyTag>
using FederationProtocolFor = FederationGlobal<KeyTag>;

template <typename KeyTag = AnyFederationKey>
using SenderProto = project_t<FederationGlobal<KeyTag>, SenderRole>;

template <typename KeyTag = AnyFederationKey>
using ReceiverProto = project_t<FederationGlobal<KeyTag>, ReceiverRole>;

template <typename KeyTag = AnyFederationKey>
using CoordProto = project_t<FederationGlobal<KeyTag>, CoordRole>;

template <typename KeyTag = AnyFederationKey>
using ExpectedSenderProto = Loop<Send<HeaderPayload<KeyTag>, Recv<Ack<KeyTag>, Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedReceiverProto = Loop<Recv<PullRequest<KeyTag>, Send<BodyPayload<KeyTag>, Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedCoordProto = Loop<
    Recv<HeaderPayload<KeyTag>, Send<Ack<KeyTag>, Send<PullRequest<KeyTag>, Recv<BodyPayload<KeyTag>, Continue>>>>>;

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
struct role_protocol_matches : std::false_type {};

template <typename KeyTag>
struct role_protocol_matches<SenderRole, SenderProto<KeyTag>, KeyTag> : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<ReceiverRole, ReceiverProto<KeyTag>, KeyTag> : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<CoordRole, CoordProto<KeyTag>, KeyTag> : std::true_type {};

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
inline constexpr bool role_protocol_matches_v = role_protocol_matches<Role, Proto, KeyTag>::value;

// Every per-role mint takes a share of the admittance permission for the remote
// organization. A share proves that the local peer was admitted to that
// organization. It also proves that a live guard holds the pool refcount above
// zero. The share is fractional rather than exclusive, so one admittance backs
// many mints through that refcount instead of by aliasing one token.
//
// The organization tag rides in the witness type, so a share for one peer does
// not type-check against a mint for another.

template <typename Org>
[[nodiscard]] constexpr auto mint_federation_pool(
    ::crucible::safety::Permission<::crucible::permissions::tag::FederatedPeer<Org>>&& admittance) noexcept {
    return ::crucible::safety::SharedPermissionPool<::crucible::permissions::tag::FederatedPeer<Org>>{
        std::move(admittance)};
}

template <typename Org, typename KeyTag = AnyFederationKey, typename Ctx, typename SenderEndpoint>
    requires CtxFitsFederation<Ctx>
[[nodiscard]] constexpr auto mint_sender(
    Ctx const& ctx, SenderEndpoint&& sender_endpoint,
    ::crucible::safety::SharedPermission<::crucible::permissions::tag::FederatedPeer<Org>> /*admittance*/) noexcept {
    using ctx_row = typename Ctx::row_type;
    using offending = ::crucible::effects::row_difference_t<federation_required_row, ctx_row>;
    CRUCIBLE_ROW_MISMATCH_ASSERT((::crucible::decide::row_subset<federation_required_row, ctx_row>()),
                                 EffectRowMismatch, &federation_mint_boundary, ctx_row, federation_required_row,
                                 offending);
    return ::crucible::safety::proto::mint_permissioned_session<SenderProto<KeyTag>>(
        ctx, std::forward<SenderEndpoint>(sender_endpoint));
}

template <typename Org, typename KeyTag = AnyFederationKey, typename Ctx, typename ReceiverEndpoint>
    requires CtxFitsFederation<Ctx>
[[nodiscard]] constexpr auto mint_receiver(
    Ctx const& ctx, ReceiverEndpoint&& receiver_endpoint,
    ::crucible::safety::SharedPermission<::crucible::permissions::tag::FederatedPeer<Org>> /*admittance*/) noexcept {
    using ctx_row = typename Ctx::row_type;
    using offending = ::crucible::effects::row_difference_t<federation_required_row, ctx_row>;
    CRUCIBLE_ROW_MISMATCH_ASSERT((::crucible::decide::row_subset<federation_required_row, ctx_row>()),
                                 EffectRowMismatch, &federation_mint_boundary, ctx_row, federation_required_row,
                                 offending);
    return ::crucible::safety::proto::mint_permissioned_session<ReceiverProto<KeyTag>>(
        ctx, std::forward<ReceiverEndpoint>(receiver_endpoint));
}

template <typename Org, typename KeyTag = AnyFederationKey, typename Ctx, typename SenderEndpoint,
          typename ReceiverEndpoint>
    requires CtxFitsFederation<Ctx>
[[nodiscard]] constexpr auto mint_channel(
    Ctx const& ctx, SenderEndpoint&& sender_endpoint, ReceiverEndpoint&& receiver_endpoint,
    ::crucible::safety::SharedPermission<::crucible::permissions::tag::FederatedPeer<Org>> admittance) noexcept {
    return std::pair{
        mint_sender<Org, KeyTag>(ctx, std::forward<SenderEndpoint>(sender_endpoint), admittance),
        mint_receiver<Org, KeyTag>(ctx, std::forward<ReceiverEndpoint>(receiver_endpoint), admittance),
    };
}

template <typename Org, typename KeyTag = AnyFederationKey, typename Ctx, typename CoordEndpoint>
    requires CtxFitsFederation<Ctx>
[[nodiscard]] constexpr auto mint_coord(
    Ctx const& ctx, CoordEndpoint&& coord_endpoint,
    ::crucible::safety::SharedPermission<::crucible::permissions::tag::FederatedPeer<Org>> /*admittance*/) noexcept {
    using ctx_row = typename Ctx::row_type;
    using offending = ::crucible::effects::row_difference_t<federation_required_row, ctx_row>;
    CRUCIBLE_ROW_MISMATCH_ASSERT((::crucible::decide::row_subset<federation_required_row, ctx_row>()),
                                 EffectRowMismatch, &federation_mint_boundary, ctx_row, federation_required_row,
                                 offending);
    return ::crucible::safety::proto::mint_permissioned_session<CoordProto<KeyTag>>(
        ctx, std::forward<CoordEndpoint>(coord_endpoint));
}

static_assert(is_global_well_formed_v<FederationGlobal<>>);
static_assert(std::is_same_v<SenderProto<>, ExpectedSenderProto<>>);
static_assert(std::is_same_v<ReceiverProto<>, ExpectedReceiverProto<>>);
static_assert(std::is_same_v<CoordProto<>, ExpectedCoordProto<>>);
static_assert(role_protocol_matches_v<SenderRole, SenderProto<>>);
static_assert(role_protocol_matches_v<ReceiverRole, ReceiverProto<>>);
static_assert(role_protocol_matches_v<CoordRole, CoordProto<>>);

}  // namespace crucible::safety::proto::federation
