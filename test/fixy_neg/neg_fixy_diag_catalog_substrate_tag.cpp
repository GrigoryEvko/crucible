// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-C8 fixture #1: substrate diagnostic tags MUST NOT
// register as fixy diagnostics.
//
// Violation: `safety::diag::HotPathViolation` is a substrate Catalog
// entry (FOUND-E01 catalog index 5).  Per the C8 reconciliation, the
// substrate Catalog and `fixy::diag::FixyCatalog` are disjoint by
// design — substrate tags carry their own Category enum and never
// appear in the per-axis FixyNotEngaged_* tag set.  `is_fixy_diag_v`
// must return FALSE here; static-asserting it true must fail.
//
// Expected diagnostic: GCC's static_assert pointing at the
// "is_fixy_diag_v<HotPathViolation>" claim.

#include <crucible/fixy/Reject.h>

namespace fd = crucible::fixy::diag;
namespace cd = crucible::safety::diag;

// Unique-carrier discipline.
struct DiagCatalogNegFixture1_Marker {};

int main() {
    static_assert(fd::is_fixy_diag_v<cd::HotPathViolation>,
        "fixy::diag::is_fixy_diag_v<HotPathViolation> must reject — "
        "substrate Catalog tags MUST NOT appear in FixyCatalog.  The "
        "two diagnostic enumerations are disjoint by design "
        "(FOUND-E01 + FIXY-AUDIT-C8).");
    (void)sizeof(DiagCatalogNegFixture1_Marker);
    return 0;
}
