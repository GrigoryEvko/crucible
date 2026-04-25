// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assert_associated<Γ, G, SessionTag> where Γ is missing
// a role that G has.  L5 SessionAssoc.h's assertion helper fires
// its classified static_assert (domain_matches_v fails condition 1).

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>

using namespace crucible::safety::proto;

struct My2PC    {};
struct Coord    {};
struct Follower {};
struct Prepare  {};
struct Vote     {};

using G = Transmission<Coord, Follower, Prepare,
          Transmission<Follower, Coord, Vote, End_G>>;

// Γ is missing the Follower entry — G's roles = {Coord, Follower},
// Γ's roles = {Coord} only.  Domain mismatch.
using IncompleteΓ = Context<
    Entry<My2PC, Coord, project_t<G, Coord>>>;

int main() {
    // assert_associated fires the condition-(1) static_assert.
    assert_associated<IncompleteΓ, G, My2PC>();
    return 0;
}
