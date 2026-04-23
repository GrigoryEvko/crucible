// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: compose_context_t<Γ1, Γ2> where Γ1 and Γ2 share one or
// more (session, role) keys.  L2 SessionContext.h's ComposeContext
// static_assert (CSL frame rule — disjoint contexts required)
// rejects.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionContext.h>

using namespace crucible::safety::proto;

struct SessA  {};
struct RoleA  {};
struct RoleB  {};

using GammaLeft = Context<
    Entry<SessA, RoleA, End>,
    Entry<SessA, RoleB, End>>;

// Composing Γ with itself — entries collide.
using GammaRight = Context<Entry<SessA, RoleA, Send<int, End>>>;

int main() {
    using Composed = compose_context_t<GammaLeft, GammaRight>;
    static_assert(context_size_v<Composed> == 3);
    return 0;
}
