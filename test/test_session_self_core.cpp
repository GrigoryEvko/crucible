// Focused session self-test TU (#372 SEPLOG-PERF-2).
//
// The session framework headers gate expensive compile-time witnesses
// behind CRUCIBLE_SESSION_SELF_TESTS.  Keep this group small so Ninja can
// parallelize the invariant harness instead of building one huge TU.

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
