// Focused session-pattern self-test TU for subtype-refinement invariants.

#define CRUCIBLE_SESSION_SELF_TESTS 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_SUBTYPE 1

#include <crucible/sessions/SessionPatterns.h>

#include <cstdio>

int main() {
    std::puts("session_self_patterns_subtype: framework invariants OK");
    return 0;
}
