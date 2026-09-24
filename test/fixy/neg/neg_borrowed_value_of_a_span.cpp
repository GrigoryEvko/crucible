// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// borrowed_value_t reads the element type of a Borrowed.  A std::span
// has an element_type member too, but it is not a Borrowed, and a gate
// that took it would admit an untagged view where a tagged one is asked
// for.  The requires-clause of the alias refuses it.
//
// Expected diagnostic: the template constraint of borrowed_value_t is not
// satisfied, and the note names IsBorrowed.

#include <fixy/Borrowed.h>

#include <span>

int main() {
    [[maybe_unused]] ::fixy::borrowed_value_t<std::span<int>> element = 0;
    return 0;
}
