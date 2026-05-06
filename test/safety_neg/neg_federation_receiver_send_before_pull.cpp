#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = crucible::safety::proto::federation;

struct Key {};
struct Endpoint {};

template <typename Handle>
concept CanSendBeforePull = requires(Handle h) {
    std::move(h).send(fp::BodyPayload<Key>{},
                      [](Endpoint&, fp::BodyPayload<Key>&&) noexcept {});
};

using ReceiverHandle = decltype(fp::mint_receiver<Key>(Endpoint{}));

static_assert(CanSendBeforePull<ReceiverHandle>,
    "FederationReceiver_SendBeforePull_Rejected");

int main() { return 0; }
