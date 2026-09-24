// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A braced list is a temporary array that dies at the end of the
// statement.  std::span of a const element type takes a braced list, so
// without the deleted initializer_list constructor of Borrowed, the call
// below takes the span constructor and returns a borrow that dangles.
// The rvalue-range twin cannot catch it, because a braced list deduces
// no range type.
//
// Expected diagnostic: the call selects the deleted initializer_list
// constructor, and the compiler repeats its reason.

#include <fixy/Borrowed.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::Borrowed<int const, Owner> dangling({1, 2, 3});
    return 0;
}
