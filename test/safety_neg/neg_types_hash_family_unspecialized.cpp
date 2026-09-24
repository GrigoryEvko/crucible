// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Types-3 #1069, mismatch class #1 of 2:
// UNSPECIALIZED TYPE HAS NO HASH-FAMILY MAPPING.
//
// The hash_family_of<T> primary template is INCOMPLETE on purpose —
// any T that hasn't been explicitly declared a member of FamilyA or
// FamilyB hits an "incomplete type" / "no member named 'family'"
// instantiation error.  This forces every new strong-hash addition
// to declare its family at the definition site (Types.h:266-273),
// rather than silently defaulting into one lane.
//
// If this fixture starts compiling, the family-discrimination
// surface degraded to a permissive default (e.g., someone changed
// the primary template to ship a fallback `using family = FamilyA`)
// and the load-bearing "every hash declares its family explicitly"
// invariant disappeared.
//
// Distinct from the cross-family-contradiction fixture, which fails
// because TWO families collide; here the failure is the ABSENCE of
// any family specialization at all.
//
// Expected diagnostic: incomplete type / no member named 'family' /
// no type named 'family' / constraints not satisfied.

#include <crucible/Types.h>

int main() {
    // Should FAIL: `int` has no hash_family_of specialization;
    // hash_family_of_t<int> hits the incomplete primary template.
    using NoFamily = ::crucible::hash_family_of_t<int>;
    NoFamily probe{};
    (void)probe;
    return 0;
}
