// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-SOURCE fixture #1: Tagged<T, source::*> rejects Tagged<T,
// source::*>-with-different-tag at assignment / overload-resolution
// time.  Cross-axis mismatch through the fixy::tags alias must reject
// identically to the substrate.
//
// Violation: `Tagged<int, source::FromUser>` and `Tagged<int,
// source::Sanitized>` are DISTINCT specializations.  Direct
// `static_assert(std::is_same_v<...>)` of the two types is false.
//
// Expected diagnostic: GCC's static_assert pointing at the
// std::is_same_v claim with two distinct Tagged specializations.

#include <crucible/fixy/Source.h>
#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace ft = crucible::fixy::tags;

// Unique-carrier discipline.
struct SourceNegFixture1_Marker {};

int main() {
    using FromUserTagged   = crucible::safety::Tagged<int, ft::source::FromUser>;   // FIXY-DISCIPLINE-OK: neg fixture exercises substrate Tagged<> directly to demonstrate alias-preserved type identity
    using SanitizedTagged  = crucible::safety::Tagged<int, ft::source::Sanitized>;  // FIXY-DISCIPLINE-OK: neg fixture exercises substrate Tagged<> directly to demonstrate alias-preserved type identity
    static_assert(std::is_same_v<FromUserTagged, SanitizedTagged>,
        "Tagged<int, source::FromUser> and Tagged<int, source::Sanitized> "
        "must be the SAME type — fixy::tags alias must collapse source "
        "tags.  Cross-axis mismatch through the alias.");
    (void)sizeof(SourceNegFixture1_Marker);
    return 0;
}
