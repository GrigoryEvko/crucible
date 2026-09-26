// The session framework headers gate expensive compile-time witnesses behind
// CRUCIBLE_SESSION_SELF_TESTS.  Each group stays small so the build compiles
// the invariant harness in parallel instead of as one large TU.

#define CRUCIBLE_SESSION_SELF_TESTS 1

#include <crucible/sessions/_Session.h>
#include <crucible/sessions/_SessionAssoc.h>
#include <crucible/sessions/_SessionContext.h>
#include <crucible/sessions/_SessionGlobal.h>
#include <crucible/sessions/_SessionQueue.h>

#include <cstdio>

int main() {
    std::puts("session_self_core: framework invariants OK");
    return 0;
}
