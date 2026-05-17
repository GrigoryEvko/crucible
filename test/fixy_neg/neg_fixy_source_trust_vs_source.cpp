// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-SOURCE fixture #2: source:: and trust:: tags occupy ORTHOGONAL
// axes — a source::FromUser is NOT the same type as trust::Verified,
// even though both are empty structs.  Cross-axis confusion via the
// fixy::tags alias must reject identically to the substrate.
//
// Violation: `is_same_v<source::FromUser, trust::Verified>` is false;
// static-asserting it holds is a hard compile error.
//
// Expected diagnostic: GCC's static_assert pointing at the
// "is_same_v<source::FromUser, trust::Verified>" claim.

#include <crucible/fixy/Source.h>

#include <type_traits>

namespace ft = crucible::fixy::tags;

// Unique-carrier discipline.
struct SourceNegFixture2_Marker {};

int main() {
    static_assert(std::is_same_v<ft::source::FromUser, ft::trust::Verified>,
        "source::FromUser and trust::Verified must be the SAME type — "
        "fixy::tags alias must collapse cross-axis tag identities.  "
        "(Orthogonal axes must remain distinct.)");
    (void)sizeof(SourceNegFixture2_Marker);
    return 0;
}
