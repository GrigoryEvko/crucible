#include <crucible/sessions/FederationProtocol.h>
#include <crucible/permissions/FederationPermission.h>

#include <utility>

namespace fp = crucible::safety::proto::federation;

struct ExpectedKey {};
struct WrongKey {};
struct PeerOrg {};
struct Endpoint {};

// fixy-CR-07: federation session mints now take an `Org` first template
// parameter and a `Permission<FederatedPeer<Org>> const&` admittance
// witness.  This fixture is a concept probe — admittance threaded via
// `std::declval` inside `decltype` (compile-time only context).
using Admittance =
    crucible::safety::Permission<
        crucible::permissions::tag::FederatedPeer<PeerOrg>>;

using SenderHandle = decltype(fp::mint_sender<PeerOrg, ExpectedKey>(
    std::declval<crucible::effects::HotFgCtx const&>(),
    Endpoint{},
    std::declval<Admittance const&>()));

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
