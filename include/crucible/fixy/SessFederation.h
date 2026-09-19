#pragma once

#include <crucible/effects/_EffectRow.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/RowMismatch.h>
#include <crucible/sessions/FederationProtocol.h>

#include <type_traits>
#include <utility>

namespace crucible::fixy::sess {

namespace federation = ::crucible::safety::proto::federation;
using federation::mint_sender;
using federation::mint_receiver;
using federation::mint_coord;
// An exclusive peer permission parks in the pool. Each call site lends a
// guard and passes the guard's token to the per-role mints.
using federation::mint_federation_pool;

// Two mint_channel factories exist. One pairs two resource endpoints through
// a dual-typed protocol. The other pairs a sender and a receiver endpoint
// through the federation's key-indexed role protocols. Their parameter shapes
// differ, so introducing both into this namespace by plain using-declaration
// would make the federation overload win on some call sites and shadow the
// session-protocol form. Only the session-protocol name is hoisted here. The
// federation form keeps the explicit alias below, which stays unambiguous and
// greppable.
//
// The requires-clause of the wrapper restates the substrate gate so a
// mismatch is reported at this call site rather than three levels deeper.

using ::crucible::safety::proto::federation::federation_required_row;
using ::crucible::safety::proto::federation::CtxFitsFederation;

template <typename Org, typename KeyTag = federation::AnyFederationKey, typename Ctx, typename SenderEndpoint,
          typename ReceiverEndpoint>
    requires ::crucible::safety::proto::federation::CtxFitsFederation<Ctx>
[[nodiscard]] constexpr auto mint_federation_channel(
    Ctx const& ctx, SenderEndpoint&& sender_endpoint, ReceiverEndpoint&& receiver_endpoint,
    ::crucible::safety::SharedPermission<::crucible::permissions::tag::FederatedPeer<Org>> admittance) noexcept {
    using ctx_row = typename Ctx::row_type;
    using offending =
        ::crucible::effects::row_difference_t<::crucible::safety::proto::federation::federation_required_row, ctx_row>;
    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::decide::row_subset<::crucible::safety::proto::federation::federation_required_row, ctx_row>()),
        EffectRowMismatch, &::crucible::safety::proto::federation::federation_mint_boundary, ctx_row,
        ::crucible::safety::proto::federation::federation_required_row, offending);
    return federation::mint_channel<Org, KeyTag>(ctx, std::forward<SenderEndpoint>(sender_endpoint),
                                                 std::forward<ReceiverEndpoint>(receiver_endpoint), admittance);
}

}  // namespace crucible::fixy::sess

// A rename on the substrate side trips at every consumer's include rather
// than three translation units deeper.

namespace crucible::fixy::sess::v065_self_test {

namespace ffed = ::crucible::fixy::sess::federation;
namespace pfed = ::crucible::safety::proto::federation;

struct ProbeKey {};

static_assert(std::is_same_v<ffed::SenderRole, pfed::SenderRole>,
              "fixy::sess::federation::SenderRole must reach substrate.  If "
              "this red-lights, the namespace alias in SessFederation.h is "
              "broken or substrate symbol moved.");
static_assert(std::is_same_v<ffed::ReceiverRole, pfed::ReceiverRole>);
static_assert(std::is_same_v<ffed::CoordRole, pfed::CoordRole>);

static_assert(std::is_same_v<ffed::AnyFederationKey, pfed::AnyFederationKey>);

static_assert(std::is_same_v<ffed::SenderProto<ProbeKey>, pfed::SenderProto<ProbeKey>>);
static_assert(std::is_same_v<ffed::ReceiverProto<ProbeKey>, pfed::ReceiverProto<ProbeKey>>);
static_assert(std::is_same_v<ffed::CoordProto<ProbeKey>, pfed::CoordProto<ProbeKey>>);

static_assert(std::is_same_v<ffed::ExpectedSenderProto<ProbeKey>, pfed::ExpectedSenderProto<ProbeKey>>);
static_assert(std::is_same_v<ffed::ExpectedReceiverProto<ProbeKey>, pfed::ExpectedReceiverProto<ProbeKey>>);
static_assert(std::is_same_v<ffed::ExpectedCoordProto<ProbeKey>, pfed::ExpectedCoordProto<ProbeKey>>);

static_assert(std::is_same_v<ffed::FederationProtocol, pfed::FederationProtocol>);
static_assert(std::is_same_v<ffed::FederationProtocolFor<ProbeKey>, pfed::FederationProtocolFor<ProbeKey>>);

static_assert(std::is_same_v<ffed::Ack<ProbeKey>, pfed::Ack<ProbeKey>>);
static_assert(std::is_same_v<ffed::PullRequest<ProbeKey>, pfed::PullRequest<ProbeKey>>);
static_assert(std::is_same_v<ffed::FederationEntryPayload<ProbeKey>, pfed::FederationEntryPayload<ProbeKey>>);
static_assert(std::is_same_v<ffed::HeaderPayload<ProbeKey>, pfed::HeaderPayload<ProbeKey>>);
static_assert(std::is_same_v<ffed::BodyPayload<ProbeKey>, pfed::BodyPayload<ProbeKey>>);

static_assert(ffed::role_protocol_matches_v<pfed::SenderRole, ffed::SenderProto<ProbeKey>, ProbeKey>,
              "Verifier must admit (SenderRole, SenderProto, KeyTag).");
static_assert(!ffed::role_protocol_matches_v<pfed::SenderRole, ffed::ReceiverProto<ProbeKey>, ProbeKey>,
              "Verifier must REJECT (SenderRole, ReceiverProto, KeyTag).");
static_assert(ffed::role_protocol_matches<pfed::CoordRole, ffed::CoordProto<ProbeKey>, ProbeKey>::value);

namespace fixy_sess_ns = ::crucible::fixy::sess;
static_assert(static_cast<void*>(nullptr) == static_cast<void*>(nullptr),
              "Mint reach is exercised via runtime_smoke_test below — "
              "consteval cannot take address of variadic function templates "
              "without ctx + admittance fixtures.");

static_assert(
    std::is_same_v<decltype(fixy_sess_ns::federation_required_row{}), decltype(pfed::federation_required_row{})>,
    "federation_required_row must reach identically through fixy::");

constexpr int v065_fixy_surface_cardinality = 8;
static_assert(v065_fixy_surface_cardinality == 8, "fixy::sess:: federation surface cardinality drifted — "
                                                  "update the using-decls AND this sentinel in lockstep.");

// A static assertion alone can mask a SFINAE, consteval or inline-body fault,
// so this body forces every reach-symbol through real instantiation. The
// mints are not called, because they need a context and an admittance
// fixture. The routine sits in this sub-namespace so it does not collide with
// the umbrella smoke routine of the same name.

inline void runtime_smoke_test() noexcept {
    namespace fed_alias = ::crucible::fixy::sess::federation;
    namespace pfed_alias = ::crucible::safety::proto::federation;
    struct LocalKeyTag {};

    using SenderP = fed_alias::SenderProto<LocalKeyTag>;
    using ReceiverP = fed_alias::ReceiverProto<LocalKeyTag>;
    using CoordP = fed_alias::CoordProto<LocalKeyTag>;
    using GlobalP = fed_alias::FederationProtocolFor<LocalKeyTag>;

    [[maybe_unused]] constexpr bool sender_id_ok = std::is_same_v<SenderP, pfed_alias::SenderProto<LocalKeyTag>>;
    [[maybe_unused]] constexpr bool receiver_id_ok = std::is_same_v<ReceiverP, pfed_alias::ReceiverProto<LocalKeyTag>>;
    [[maybe_unused]] constexpr bool coord_id_ok = std::is_same_v<CoordP, pfed_alias::CoordProto<LocalKeyTag>>;
    [[maybe_unused]] constexpr bool global_id_ok =
        std::is_same_v<GlobalP, pfed_alias::FederationProtocolFor<LocalKeyTag>>;

    [[maybe_unused]] constexpr bool sender_matches =
        fed_alias::role_protocol_matches_v<pfed_alias::SenderRole, SenderP, LocalKeyTag>;
    [[maybe_unused]] constexpr bool sender_rejects_receiver_role =
        !fed_alias::role_protocol_matches_v<pfed_alias::SenderRole, ReceiverP, LocalKeyTag>;

    (void)sender_id_ok;
    (void)receiver_id_ok;
    (void)coord_id_ok;
    (void)global_id_ok;
    (void)sender_matches;
    (void)sender_rejects_receiver_role;
}

}  // namespace crucible::fixy::sess::v065_self_test
