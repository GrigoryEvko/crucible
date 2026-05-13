// Focused session self-test TU for crash and checkpoint invariants.

#define CRUCIBLE_SESSION_SELF_TESTS 1

#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionDiagnostic.h>

#include <cstdio>

int main() {
    std::puts("session_self_crash: framework invariants OK");
    return 0;
}
