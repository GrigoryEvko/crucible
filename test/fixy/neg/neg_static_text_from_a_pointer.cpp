// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A pointer of the run time can name a text that ends before the program.
// The type cannot tell a pointer to a literal from such a pointer, so no
// pointer makes a StaticText, also the pointer of a literal.
//
// Expected diagnostic: the deleted form of a pointer gives its reason.

#include <foundation/core/StaticText.h>

int main() {
    ::foundation::core::StaticText const text{+"abc"};
    return static_cast<int>(text.view().size());
}
