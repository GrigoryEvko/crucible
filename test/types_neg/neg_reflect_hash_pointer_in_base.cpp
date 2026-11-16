// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// reflect_hash folds each base of a class, so it refuses the state of a base
// that it refuses in the class itself.  A View holds an address, and an
// address changes from run to run, so a hash of it breaks the bit-exact
// replay.  The walks of the bases and of the fields refuse the View.  A fold
// that skips the bases compiles this file, and it gives one hash for two
// values that differ only in the field of the base.

#include <crucible/Reflect.h>

struct HoldsAddress {
    ::fixy::View<int const> elements{};
};
struct DerivedFromAddress : HoldsAddress {
    unsigned own = 0;
};

int main() {
    DerivedFromAddress const value{};
    return static_cast<int>(crucible::reflect_hash(value) & 1u);
}
