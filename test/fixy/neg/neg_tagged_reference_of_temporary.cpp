// Negative-compile test. This file must fail to compile.
//
// Violation: the tag of an object by reference names a temporary.  The tag
// names the object, and the temporary ends at the end of the full
// expression, so the tag would dangle.  The overload for an rvalue is
// deleted.

#include <fixy/Tagged.h>

int main() {
    auto const tagged = ::fixy::mint_tagged<::fixy::tags::source::External, int const&>(5);
    return tagged.value() == 5 ? 0 : 1;
}
