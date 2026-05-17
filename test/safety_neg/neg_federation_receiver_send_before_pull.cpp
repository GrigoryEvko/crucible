#include <crucible/sessions/FederationProtocol.h>
#include <crucible/permissions/FederationPermission.h>

#include <utility>

namespace fp = crucible::safety::proto::federation;

struct Key {};
struct PeerOrg {};
struct Endpoint {};

// fixy-CR-07: federation session mints now take an `Org` first template
// parameter and a `Permission<FederatedPeer<Org>> const&` admittance
// witness.  This fixture is a concept probe over the derived handle
// type — we use `std::declval` for the admittance slot since the
// expression lives entirely inside `decltype` (compile-time only).
using Admittance =
    crucible::safety::Permission<
        crucible::permissions::tag::FederatedPeer<PeerOrg>>;

template <typename Handle>
concept CanSendBeforePull = requires(Handle h) {
    std::move(h).send(fp::BodyPayload<Key>{},
                      [](Endpoint&, fp::BodyPayload<Key>&&) noexcept {});
};

using ReceiverHandle = decltype(fp::mint_receiver<PeerOrg, Key>(
    std::declval<crucible::effects::HotFgCtx const&>(),
    Endpoint{},
    std::declval<Admittance const&>()));

static_assert(CanSendBeforePull<ReceiverHandle>,
    "FederationReceiver_SendBeforePull_Rejected");

int main() { return 0; }
