// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: using diagnostic_name_v<T> where T is not a
// diagnostic class.  The accessor's requires-clause rejects.

#include <crucible/safety/SessionDiagnostic.h>

using namespace crucible::safety::proto::diagnostic;

struct NotADiagnostic {};  // doesn't inherit from tag_base

int main() {
    // NotADiagnostic isn't a diagnostic class — the requires-clause
    // on diagnostic_name_v fails, firing a compile error.
    constexpr auto n = diagnostic_name_v<NotADiagnostic>;
    (void)n;
    return 0;
}
