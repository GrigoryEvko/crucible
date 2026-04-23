// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: lookup_context_t<Γ, S, R> where (S, R) is not in Γ.
// L2 SessionContext.h's LookupContext<Context<>, ...> primary
// specialisation fires its dependent-false static_assert.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionContext.h>

using namespace crucible::safety::proto;

struct SessX {};
struct RoleA {};
struct RoleB {};

using MyΓ = Context<Entry<SessX, RoleA, End>>;

int main() {
    // Looking up (SessX, RoleB) which isn't in Γ — fires the
    // LookupContext static_assert.
    using MissingT = lookup_context_t<MyΓ, SessX, RoleB>;
    (void)static_cast<MissingT*>(nullptr);
    return 0;
}
