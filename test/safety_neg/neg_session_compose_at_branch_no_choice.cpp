// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `compose_at_branch_t<P, I, Q>` invoked on a P that has
// no Select<Bs...> or Offer<Bs...> reachable from its head spine.
// Per #378 the static_assert fires `[Branch_Compose_No_Choice]`.

#include <crucible/safety/Session.h>

using namespace crucible::safety::proto;

struct Req {};
struct Followup {};

// Spine is Send -> Recv -> End — no Select/Offer at all.
using LinearProtocol = Send<Req, Recv<int, End>>;

void compile_time_reject() {
    using Bad = compose_at_branch_t<LinearProtocol, /*branch=*/0,
                                    Recv<Followup, End>>;
    (void)sizeof(Bad);
}

int main() { return 0; }
