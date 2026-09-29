// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A keyed reception comes from the role that its PeerMsg names.  Here the
// message comes from q, the cell watches r, and q is not reliable, so a
// crash of q reaches no detector and the reception waits for ever.  The
// crash walk reads the role of the keyed step and refuses the protocol,
// although the caller named r as the peer and declared r reliable.
//
// Expected diagnostic: no mint_crash_session matches, because the coverage
// clause of the crash walk evaluated to false: the reception from q has no
// crash branch, and q is not reliable.
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Ctx.h>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace neg_sess_crash_keyed_sender_unwatched_types {
struct P {};
struct Q {};
struct R {};
struct Hello {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using FromQ = s::Recv<s::PeerMsg<Q, Hello, int>, s::End>;
}  // namespace neg_sess_crash_keyed_sender_unwatched_types

int main() {
    using namespace neg_sess_crash_keyed_sender_unwatched_types;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    const s::PeerCrashCell cell_of_r;
    s::PeerCrashCell cell_of_p;
    auto watched =
        s::mint_crash_session<FromQ, P, R, s::ReliableSet<R>>(ctx, Wire{}, cell_of_r, s::mint_crash_writer(cell_of_p));
    static_cast<void>(watched);
    return 0;
}
