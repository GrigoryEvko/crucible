// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-C8 fixture #2: arbitrary user types MUST NOT register as
// fixy diagnostics.
//
// Violation: `int` is not a member of `fixy::diag::FixyCatalog`.  The
// catalog is closed to the twenty `FixyNotEngaged_*` tag classes;
// `is_fixy_diag_v<T>` is the structural witness that T appears in the
// catalog tuple.  Anything outside the closed set must reject.
//
// Distinct from the substrate-tag fixture: this exercises the
// "user-defined / primitive type does NOT shortcut into the catalog"
// rejection class.  Catches authors that confuse tag_base inheritance
// (an OPEN concept) with FixyCatalog membership (a CLOSED concept).
//
// Expected diagnostic: GCC's static_assert pointing at the
// "is_fixy_diag_v<int>" claim.

#include <crucible/fixy/Reject.h>

namespace fd = crucible::fixy::diag;

// Unique-carrier discipline.
struct DiagCatalogNegFixture2_Marker {};

int main() {
    static_assert(fd::is_fixy_diag_v<int>,
        "fixy::diag::is_fixy_diag_v<int> must reject — int is not a "
        "FixyCatalog entry.  FixyCatalog is closed to the twenty "
        "FixyNotEngaged_<Axis> tag classes; inheriting tag_base is "
        "NOT sufficient for catalog membership.");
    (void)sizeof(DiagCatalogNegFixture2_Marker);
    return 0;
}
