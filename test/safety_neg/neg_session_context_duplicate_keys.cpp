// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: Context<Entries...> with two entries sharing the same
// (session, role) key pair.  L2 SessionContext.h's static_assert
// on all_keys_distinct_v rejects.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionContext.h>

using namespace crucible::safety::proto;

struct SessA {};
struct RoleA {};

// Two entries with identical (SessA, RoleA) keys — ambiguous.
using BadGamma = Context<
    Entry<SessA, RoleA, End>,
    Entry<SessA, RoleA, Send<int, End>>>;

int main() {
    // BadGamma's static_assert fires at instantiation.
    static_assert(context_size_v<BadGamma> == 2);
    return 0;
}
