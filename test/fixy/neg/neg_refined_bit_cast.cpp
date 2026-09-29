// mint_refined runs the predicate, and the value constructor is private.
// A trivially copyable Refined would leave a byte route around both:
// std::bit_cast<Refined<positive, int>>(-1) would build a positive int
// that holds -1.  The assignments are user-provided, so the class is not
// trivially copyable and bit_cast refuses it at its constraint.

#include <fixy/Refined.h>

#include <bit>

int main() {
    auto forged = std::bit_cast<fixy::Refined<fixy::positive, int>>(-1);
    return forged.value();
}
