#pragma once

// crucible::safety::proto::federation
//
// MPST facade for Cipher federation.  The byte-stable codec remains
// in cipher/FederationProtocol.h; this header describes the legal
// protocol order around that codec:
//
//   Sender  -> Coord    : ContentAddressed<FederationEntryHeader>
//   Coord   -> Sender   : Ack<KeyTag>
//   Coord   -> Receiver : PullRequest<KeyTag>
//   Receiver-> Coord    : ContentAddressed<FederationEntryPayload<KeyTag>>
//   repeat
//
// The projected Sender and Receiver views are the production-facing
// handles.  Coord is included so the global is honest three-party MPST
// rather than two unrelated binary protocols.

#include <crucible/Types.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionMint.h>

#include <span>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::federation {

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
using HeaderPayload =
    ContentAddressed<::crucible::cipher::federation::FederationEntryHeader>;

template <typename KeyTag = AnyFederationKey>
using BodyPayload = ContentAddressed<FederationEntryPayload<KeyTag>>;

template <typename KeyTag = AnyFederationKey>
using FederationGlobal = Rec_G<
    Transmission<SenderRole, CoordRole, HeaderPayload<KeyTag>,
    Transmission<CoordRole, SenderRole, Ack<KeyTag>,
    Transmission<CoordRole, ReceiverRole, PullRequest<KeyTag>,
    Transmission<ReceiverRole, CoordRole, BodyPayload<KeyTag>,
    Var_G>>>>>;

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
using ExpectedSenderProto = Loop<Send<HeaderPayload<KeyTag>,
                                      Recv<Ack<KeyTag>, Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedReceiverProto = Loop<Recv<PullRequest<KeyTag>,
                                        Send<BodyPayload<KeyTag>, Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedCoordProto = Loop<Recv<HeaderPayload<KeyTag>,
                                    Send<Ack<KeyTag>,
                                    Send<PullRequest<KeyTag>,
                                    Recv<BodyPayload<KeyTag>, Continue>>>>>;

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
struct role_protocol_matches : std::false_type {};

template <typename KeyTag>
struct role_protocol_matches<SenderRole, SenderProto<KeyTag>, KeyTag>
    : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<ReceiverRole, ReceiverProto<KeyTag>, KeyTag>
    : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<CoordRole, CoordProto<KeyTag>, KeyTag>
    : std::true_type {};

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
inline constexpr bool role_protocol_matches_v =
    role_protocol_matches<Role, Proto, KeyTag>::value;

template <typename KeyTag = AnyFederationKey,
          ::crucible::effects::IsExecCtx Ctx,
          typename SenderEndpoint>
[[nodiscard]] constexpr auto mint_sender(
    Ctx const& ctx,
    SenderEndpoint&& sender_endpoint) noexcept {
    return ::crucible::safety::proto::mint_session<SenderProto<KeyTag>>(
        ctx, std::forward<SenderEndpoint>(sender_endpoint));
}

template <typename KeyTag = AnyFederationKey, typename SenderEndpoint>
[[nodiscard]] constexpr auto mint_sender(
    SenderEndpoint&& sender_endpoint) noexcept {
    const ::crucible::effects::HotFgCtx ctx{};
    return mint_sender<KeyTag>(
        ctx, std::forward<SenderEndpoint>(sender_endpoint));
}

template <typename KeyTag = AnyFederationKey,
          ::crucible::effects::IsExecCtx Ctx,
          typename ReceiverEndpoint>
[[nodiscard]] constexpr auto mint_receiver(
    Ctx const& ctx,
    ReceiverEndpoint&& receiver_endpoint) noexcept {
    return ::crucible::safety::proto::mint_session<ReceiverProto<KeyTag>>(
        ctx, std::forward<ReceiverEndpoint>(receiver_endpoint));
}

template <typename KeyTag = AnyFederationKey, typename ReceiverEndpoint>
[[nodiscard]] constexpr auto mint_receiver(
    ReceiverEndpoint&& receiver_endpoint) noexcept {
    const ::crucible::effects::HotFgCtx ctx{};
    return mint_receiver<KeyTag>(
        ctx, std::forward<ReceiverEndpoint>(receiver_endpoint));
}

template <typename KeyTag = AnyFederationKey,
          ::crucible::effects::IsExecCtx Ctx,
          typename SenderEndpoint,
          typename ReceiverEndpoint>
[[nodiscard]] constexpr auto mint_channel(
    Ctx const& ctx,
    SenderEndpoint&& sender_endpoint,
    ReceiverEndpoint&& receiver_endpoint) noexcept {
    return std::pair{
        mint_sender<KeyTag>(
            ctx, std::forward<SenderEndpoint>(sender_endpoint)),
        mint_receiver<KeyTag>(
            ctx, std::forward<ReceiverEndpoint>(receiver_endpoint)),
    };
}

template <typename KeyTag = AnyFederationKey,
          typename SenderEndpoint,
          typename ReceiverEndpoint>
[[nodiscard]] constexpr auto mint_channel(
    SenderEndpoint&& sender_endpoint,
    ReceiverEndpoint&& receiver_endpoint) noexcept {
    const ::crucible::effects::HotFgCtx ctx{};
    return mint_channel<KeyTag>(
        ctx,
        std::forward<SenderEndpoint>(sender_endpoint),
        std::forward<ReceiverEndpoint>(receiver_endpoint));
}

template <typename KeyTag = AnyFederationKey,
          ::crucible::effects::IsExecCtx Ctx,
          typename CoordEndpoint>
[[nodiscard]] constexpr auto mint_coord(
    Ctx const& ctx,
    CoordEndpoint&& coord_endpoint) noexcept {
    return ::crucible::safety::proto::mint_session<CoordProto<KeyTag>>(
        ctx, std::forward<CoordEndpoint>(coord_endpoint));
}

template <typename KeyTag = AnyFederationKey, typename CoordEndpoint>
[[nodiscard]] constexpr auto mint_coord(CoordEndpoint&& coord_endpoint) noexcept {
    const ::crucible::effects::HotFgCtx ctx{};
    return mint_coord<KeyTag>(
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
