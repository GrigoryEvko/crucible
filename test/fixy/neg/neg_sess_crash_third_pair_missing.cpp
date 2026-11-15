// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// p sends a keyed message to a reliable third role.  The message counts in
// the cell of the pair of p and the third role, so the mint takes the one
// writer of that cell.  A mint with no writer for that pair would leave the
// message with no cell to count in.
//
// Expected diagnostic: no mint_crash_session matches, because the third
// role that the mint names is not the third receiver of the protocol.
#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Ctx.h>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace neg_sess_crash_third_pair_missing_types {
struct P {};
struct Q {};
struct Third {};
struct Hello {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using ToThird = s::Send<s::PeerMsg<Third, Hello, int>, s::End>;
}  // namespace neg_sess_crash_third_pair_missing_types

int main() {
    using namespace neg_sess_crash_third_pair_missing_types;
    const ::fixy::BgDrainCtx ctx{eff::testing::bg()};
    const s::CrashCell<Q, P> cell_of_q;
    s::CrashCell<P, Q> cell_of_p;
    auto watched = s::mint_crash_session<ToThird, P, Q, s::ReliableSet<Third>>(ctx, Wire{}, cell_of_q,
                                                                               s::mint_crash_writer(cell_of_p));
    static_cast<void>(watched);
    return 0;
}
