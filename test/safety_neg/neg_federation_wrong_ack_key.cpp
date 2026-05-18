#include <crucible/sessions/FederationProtocol.h>
#include <crucible/permissions/FederationPermission.h>

#include <utility>

namespace fp  = crucible::safety::proto::federation;
namespace eff = crucible::effects;

// fixy-CR-13: federation mints require Row<IO, Block> in ctx::row_type.
using FederationFitCtx = decltype(
    eff::BgCompileCtx{}.in_row<eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc,
        eff::Effect::IO, eff::Effect::Block>>());

struct ExpectedKey {};
struct WrongKey {};
struct PeerOrg {};
struct Endpoint {};

// fixy-CR-07 + fixy-A2-009: federation session mints now take an `Org`
// first template parameter and a `SharedPermission<FederatedPeer<Org>>`
// admittance witness (by value, copyable empty proof token from a pool
// guard's `token()`).  This fixture is a concept probe — admittance
// threaded via `std::declval` inside `decltype` (compile-time only context).
using Admittance =
    crucible::safety::SharedPermission<
        crucible::permissions::tag::FederatedPeer<PeerOrg>>;

using SenderHandle = decltype(fp::mint_sender<PeerOrg, ExpectedKey>(
    std::declval<FederationFitCtx const&>(),
    Endpoint{},
    std::declval<Admittance>()));

using AfterAnnounce = decltype(
    std::declval<SenderHandle&&>().send(
        fp::HeaderPayload<ExpectedKey>{},
        [](Endpoint&, fp::HeaderPayload<ExpectedKey>&&) noexcept {}));

template <typename Handle>
concept CanReceiveWrongAck = requires(Handle h) {
    std::move(h).recv([](Endpoint&) noexcept -> fp::Ack<WrongKey> {
        return {};
    });
};

static_assert(CanReceiveWrongAck<AfterAnnounce>,
    "FederationAck_ContentHashMismatch_Rejected");

int main() { return 0; }
