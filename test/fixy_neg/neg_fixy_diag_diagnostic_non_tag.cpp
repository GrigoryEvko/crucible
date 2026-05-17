// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-DIAG fixture #2: Diagnostic<NonTag, Ctx...> wrapper rejects
// non-tag-base classes via the substrate's `requires
// is_diagnostic_class_v<DiagnosticClass>` clause.
//
// Violation: instantiating `Diagnostic<int, ...>` violates the
// concept gate — int is not derived from tag_base.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at is_diagnostic_class_v requirement.

#include <crucible/fixy/Diag.h>

namespace fd = crucible::fixy::diag;

// Unique-carrier discipline.
struct DiagNegFixture2_CtxPayload {};

// Wrapper instantiation with non-tag fires the substrate's requires
// clause at substitution time.
using DiagNegFixture2_BadDiagnostic =
    fd::Diagnostic<int, DiagNegFixture2_CtxPayload>;

int main() {
    (void)sizeof(DiagNegFixture2_BadDiagnostic);  // forces instantiation
    return 0;
}
