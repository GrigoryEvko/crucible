// The sender hands Bob an endpoint whose protocol talks to Bob.  Bob would
// hold the two ends of the inner session, so the mint refuses the protocol.

#include <fixy/session/Delegate.h>
#include <fixy/session/Projection.h>

namespace neg_sess_delegation_to_own_peer_send_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Bob {};
struct Hand {};
struct Ask {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using TalksToBob = s::Send<s::PeerMsg<Bob, Ask, int>, s::End>;
using Carried = s::DelegatedSession<TalksToBob, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
using HandsToBob = s::Send<s::PeerMsg<Bob, Hand, Carried>, s::End>;

}  // namespace neg_sess_delegation_to_own_peer_send_types

int main() {
    namespace t = neg_sess_delegation_to_own_peer_send_types;
    auto handle = t::s::mint_session_handle<t::HandsToBob, t::Wire>(t::Wire{});
    static_cast<void>(handle);
    return 0;
}
