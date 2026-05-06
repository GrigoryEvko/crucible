#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp = crucible::safety::proto::federation;

struct ExpectedKey {};
struct WrongKey {};
struct Endpoint {};

using SenderHandle = decltype(fp::mint_sender<ExpectedKey>(Endpoint{}));
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
