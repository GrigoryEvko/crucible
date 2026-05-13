// Focused session self-test TU for view, CT, and declassification invariants.

#define CRUCIBLE_SESSION_SELF_TESTS 1

#include <crucible/sessions/SessionCT.h>
#include <crucible/sessions/SessionDeclassify.h>
#include <crucible/sessions/SessionView.h>

#include <cstdio>

int main() {
    std::puts("session_self_view: framework invariants OK");
    return 0;
}
