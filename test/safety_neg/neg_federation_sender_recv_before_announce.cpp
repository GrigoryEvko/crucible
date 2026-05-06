#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = crucible::safety::proto::federation;

struct Key {};
struct Endpoint {};

template <typename Handle>
concept CanRecvBeforeAnnounce = requires(Handle h) {
    std::move(h).recv([](Endpoint&) noexcept -> fp::Ack<Key> {
        return {};
    });
};

using SenderHandle = decltype(fp::mint_sender<Key>(Endpoint{}));

static_assert(CanRecvBeforeAnnounce<SenderHandle>,
    "FederationSender_RecvBeforeAnnounce_Rejected");

int main() { return 0; }
