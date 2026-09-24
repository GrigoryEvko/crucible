// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// oob-62 fixture: ScuttlebuttDigest::count cannot be set from outside.
//
// It used to be a bare public std::uint16_t, so this assignment compiled
// and the next push() scanned and wrote past `entries`.  Reproduced
// under ASan as a `WRITE of size 40` at the push's entries[count] store.
// ScuttlebuttSlotCount has no caller-facing mutator, so the state is
// gone rather than checked for.  Release defines no _GLIBCXX_ASSERTIONS
// and FixedArray::operator[] carries no precondition, so the type is
// what holds the bound there.

#include <crucible/canopy/Scuttlebutt.h>

#include <cstdint>

int main() {
    namespace cc = crucible::canopy;
    cc::ScuttlebuttDigest<2, 2> digest{};  // capacity 4
    digest.count = std::uint16_t{41};
    return 0;
}
