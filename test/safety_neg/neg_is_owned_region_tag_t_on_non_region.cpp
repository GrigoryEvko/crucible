// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating owned_region_tag_t<T> where T is not an
// OwnedRegion specialization.  Distinct from neg_is_owned_region_
// value_t_on_non_region.cpp because the two extractors carry their
// `requires is_owned_region_v<T>` constraints independently — a
// refactor that drops the constraint from owned_region_tag_t while
// leaving owned_region_value_t correctly constrained would slip
// through the value_t neg-compile but is caught here.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsOwnedRegion.h>

int main() {
    // void is not an OwnedRegion → tag-extractor alias is ill-formed.
    using TagOf = crucible::safety::extract::owned_region_tag_t<void>;
    TagOf const* p = nullptr;
    (void)p;
    return 0;
}
