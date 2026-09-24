// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// borrowed_source_t reads the owner tag of a Borrowed.  The class below
// declares a source_type member of its own, but it is not a Borrowed, so
// the tag it names is a claim no borrow made.  The requires-clause of the
// alias refuses it.
//
// Expected diagnostic: the template constraint of borrowed_source_t is
// not satisfied, and the note names IsBorrowed.

#include <fixy/Borrowed.h>

namespace {
struct Owner {};
struct LookalikeBorrow {
    using element_type = int;
    using source_type = Owner;
};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::borrowed_source_t<LookalikeBorrow> owner{};
    return 0;
}
