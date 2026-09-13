// The session framework headers gate expensive compile-time witnesses behind
// CRUCIBLE_SESSION_SELF_TESTS.  Each group stays small so the build compiles
// the invariant harness in parallel instead of as one large TU.

#define CRUCIBLE_SESSION_SELF_TESTS 1

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionQueue.h>

#include <cstdio>

int main() {
    std::puts("session_self_core: framework invariants OK");
    return 0;
}
