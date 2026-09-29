// One branch of a Select hands Carol an endpoint whose Offer waits for
// Carol.  The Sender note names Carol as a peer of the inner session, so
// Carol would hold the two ends of it, and the mint refuses the protocol.

#include <fixy/session/Delegate.h>
#include <fixy/session/Projection.h>

namespace neg_sess_delegation_to_own_peer_in_select_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Carol {};
struct Hand {};
struct Keep {};
struct Ask {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using HearsFromCarol = s::Offer<s::Sender<Carol>, s::Recv<s::PeerMsg<Carol, Ask, int>, s::End>>;
using Carried = s::DelegatedSession<HearsFromCarol, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
using Chooses =
    s::Select<s::Send<s::PeerMsg<Carol, Keep, int>, s::End>, s::Send<s::PeerMsg<Carol, Hand, Carried>, s::End>>;

}  // namespace neg_sess_delegation_to_own_peer_in_select_types

int main() {
    namespace t = neg_sess_delegation_to_own_peer_in_select_types;
    auto handle = t::s::mint_session_handle<t::Chooses, t::Wire>(t::Wire{});
    static_cast<void>(handle);
    return 0;
}
