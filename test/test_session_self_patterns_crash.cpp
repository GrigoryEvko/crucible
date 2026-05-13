// Focused session-pattern self-test TU for crash-safety contract invariants.

#define CRUCIBLE_SESSION_SELF_TESTS 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_CRASH 1

#include <crucible/sessions/SessionPatterns.h>

#include <cstdio>

int main() {
    std::puts("session_self_patterns_crash: framework invariants OK");
    return 0;
}
