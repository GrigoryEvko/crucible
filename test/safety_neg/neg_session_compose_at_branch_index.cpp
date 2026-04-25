// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `compose_at_branch_t<P, I, Q>` invoked with I out of
// range for the reached Select<Bs...>.  Per #378 the static_assert
// fires `[Branch_Compose_Index_Out_Of_Range]`.

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

struct Req {};
struct Ok  {};
struct Followup {};

using Server = Recv<Req, Select<Send<Ok, End>>>;   // 1 branch only

void compile_time_reject() {
    using Bad = compose_at_branch_t<Server, /*branch=*/5,
                                    Recv<Followup, End>>;
    (void)sizeof(Bad);
}

int main() { return 0; }
