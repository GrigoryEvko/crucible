// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating `Diagnostic<int, ...>` where the first
// template parameter is NOT derived from `safety::diag::tag_base`.
// The `requires is_diagnostic_class_v<DiagnosticClass>` constraint
// on Diagnostic<> rejects the instantiation; this fixture pins the
// behavior so a future relaxation of the constraint surfaces here.

#include <crucible/safety/Diagnostic.h>

using crucible::safety::diag::Diagnostic;

// Intent: someone tries to wrap an unrelated type as a diagnostic
// reason.  `int` does NOT inherit tag_base; the Diagnostic<>
// constraint must reject.
using BogusDiagnostic = Diagnostic<int, float, double>;

// Force instantiation to trigger the constraint failure.
BogusDiagnostic g_bogus{};

int main() { return 0; }
