// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A keyed send goes to the role that its PeerMsg names.  Here it goes to
// q, the cell watches r, and q is not reliable, so the decorator guards
// the send with the cell of a role that does not receive it.  The crash
// walk reads the role of the keyed step and refuses the protocol.
//
// Expected diagnostic: no mint_crash_session matches, because the watch
// clause of the crash walk evaluated to false.
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Ctx.h>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace neg_sess_crash_keyed_target_unwatched_types {
struct P {};
struct Q {};
struct R {};
struct Hello {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using ToQ = s::Send<s::PeerMsg<Q, Hello, int>, s::End>;
}  // namespace neg_sess_crash_keyed_target_unwatched_types

int main() {
    using namespace neg_sess_crash_keyed_target_unwatched_types;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    const s::PeerCrashCell cell_of_r;
    s::PeerCrashCell cell_of_p;
    auto watched = s::mint_crash_session<ToQ, P, R, s::NoReliableRoles>(ctx, Wire{}, cell_of_r, s::mint_crash_writer(cell_of_p));
    static_cast<void>(watched);
    return 0;
}
