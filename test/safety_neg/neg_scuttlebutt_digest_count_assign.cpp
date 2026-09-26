// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ScuttlebuttDigest::count cannot be set from outside.  A count past the
// capacity would make the next push() scan and write past `entries`.
// ScuttlebuttSlotCount has no caller-facing mutator, so that state cannot
// be represented.  FixedArray::operator[] carries no precondition, so in
// Release the type is what holds the bound.

#include <crucible/canopy/Scuttlebutt.h>

#include <cstdint>

int main() {
    namespace cc = crucible::canopy;
    cc::ScuttlebuttDigest<2, 2> digest{};  // capacity 4
    digest.count = std::uint16_t{41};
    return 0;
}
