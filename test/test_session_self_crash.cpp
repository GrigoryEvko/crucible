#define CRUCIBLE_SESSION_SELF_TESTS 1

#include <crucible/sessions/_SessionCheckpoint.h>
#include <crucible/sessions/_SessionContentAddressed.h>
#include <crucible/sessions/_SessionCrash.h>
#include <crucible/sessions/_SessionDelegate.h>
#include <crucible/sessions/_SessionDiagnostic.h>

#include <cstdio>

int main() {
    std::puts("session_self_crash: framework invariants OK");
    return 0;
}
