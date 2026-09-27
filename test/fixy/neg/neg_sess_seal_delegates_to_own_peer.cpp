// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that a protocol which hands an endpoint to a
// peer of its own session does not, so that a mint admits it.  The query
// is a concept over the protocol walk, and a concept has no explicit
// specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Delegate.h>
#include <fixy/session/Projection.h>

namespace neg_sess_seal_delegates_to_own_peer_types {
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
struct Bob {};
struct Ask {};
struct Hand {};
using TalksToBob = ::fixy::session::Send<::fixy::session::PeerMsg<Bob, Ask, int>, ::fixy::session::End>;
using Handed = ::fixy::session::DelegatedSession<TalksToBob, Wire, ::fixy::session::DefaultAbandonmentPolicy,
                                                 ::foundation::permissions::EmptyPermSet>;
using HandsToBob = ::fixy::session::Send<::fixy::session::PeerMsg<Bob, Hand, Handed>, ::fixy::session::End>;
}  // namespace neg_sess_seal_delegates_to_own_peer_types

namespace fixy::session {
template <>
inline constexpr bool delegates_to_own_peer_v<neg_sess_seal_delegates_to_own_peer_types::HandsToBob> = false;
}  // namespace fixy::session

int main() { return 0; }
