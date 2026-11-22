// Negative compile test.  This file must fail to compile.
//
// p sends to q, its channel peer, and the cell of the pair of p and q
// counts those messages.  A third role watches p too, and the count of the
// messages of p to it is in the cell of its own pair.  The cell of p and q
// counts other messages, so the third role must not read it.  The type of
// a cell names its pair, and the mint of the third role takes only the
// cell of the pair of p and the third role.
//
// Expected diagnostic: no mint_crash_session matches, because the cell of
// the pair of p and q does not convert to the cell of the pair of p and
// the third role.
#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>

#include <foundation/effects/Ctx.h>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace neg_sess_crash_cell_of_another_pair_types {
struct P {};
struct Q {};
struct Third {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using FromPOrCrashOfP = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
}  // namespace neg_sess_crash_cell_of_another_pair_types

int main() {
    using namespace neg_sess_crash_cell_of_another_pair_types;
    const ::fixy::BgDrainCtx ctx{eff::testing::bg()};
    const s::CrashCell<P, Q> cell_of_p_for_q;
    s::CrashCell<Third, P> cell_of_third;
    auto watched = s::mint_crash_session<FromPOrCrashOfP, Third, P>(ctx, Wire{}, cell_of_p_for_q,
                                                                    s::mint_crash_writer(cell_of_third));
    static_cast<void>(watched);
    return 0;
}
