// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-DIAG fixture #1: is_diagnostic_class_v rejects non-tag types
// when routed through `fixy::diag::is_diagnostic_class_v`.
//
// Violation: a plain `int` is NOT derived from tag_base, so
// is_diagnostic_class_v<int> is false.  Static-asserting the trait
// fires a stable framework-controlled diagnostic from the
// accessor_check helper as well.
//
// Expected diagnostic: GCC's static_assert pointing at the
// "fixy::diag::is_diagnostic_class_v<int>" claim.

#include <crucible/fixy/Diag.h>

namespace fd = crucible::fixy::diag;

// Unique-carrier discipline.
struct DiagNegFixture1_Marker {};

int main() {
    static_assert(fd::is_diagnostic_class_v<int>,
        "fixy::diag::is_diagnostic_class_v<int> must reject — int "
        "is not derived from tag_base.  Alias preserves the substrate's "
        "structural diagnostic-class detection.");
    (void)sizeof(DiagNegFixture1_Marker);
    return 0;
}
