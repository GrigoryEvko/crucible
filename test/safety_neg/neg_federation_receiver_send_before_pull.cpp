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

struct Key {};
struct PeerOrg {};
struct Endpoint {};

// fixy-CR-07 + fixy-A2-009: federation session mints now take an `Org`
// first template parameter and a `SharedPermission<FederatedPeer<Org>>`
// admittance witness (by value, copyable empty proof token from a pool
// guard's `token()`).  This fixture is a concept probe over the derived
// handle type — we use `std::declval` for the admittance slot since the
// expression lives entirely inside `decltype` (compile-time only).
using Admittance =
    crucible::safety::SharedPermission<
        crucible::permissions::tag::FederatedPeer<PeerOrg>>;

template <typename Handle>
concept CanSendBeforePull = requires(Handle h) {
    std::move(h).send(fp::BodyPayload<Key>{},
                      [](Endpoint&, fp::BodyPayload<Key>&&) noexcept {});
};

using ReceiverHandle = decltype(fp::mint_receiver<PeerOrg, Key>(
    std::declval<FederationFitCtx const&>(),
    Endpoint{},
    std::declval<Admittance>()));

static_assert(CanSendBeforePull<ReceiverHandle>,
    "FederationReceiver_SendBeforePull_Rejected");

int main() { return 0; }
