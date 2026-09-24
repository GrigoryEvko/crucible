// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// borrowed_ref_value_t reads the element type of a BorrowedRef.  A
// std::span has an element_type member too, but it is not a BorrowedRef.
// The requires-clause of the alias refuses it.
//
// Expected diagnostic: the template constraint of borrowed_ref_value_t is
// not satisfied, and the note names IsBorrowedRef.

#include <fixy/Borrowed.h>

#include <span>

int main() {
    [[maybe_unused]] ::fixy::borrowed_ref_value_t<std::span<int>> element = 0;
    return 0;
}
