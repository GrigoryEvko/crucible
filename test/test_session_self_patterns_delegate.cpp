// Focused session-pattern self-test TU for delegate-compatibility invariants.

#define CRUCIBLE_SESSION_SELF_TESTS 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_DELEGATE 1

#include <crucible/sessions/SessionPatterns.h>

#include <cstdio>

int main() {
    std::puts("session_self_patterns_delegate: framework invariants OK");
    return 0;
}
