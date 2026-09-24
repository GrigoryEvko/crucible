// mint_refined runs the predicate, and the value constructor is private.
// A trivially copyable Refined left a byte route around both:
// std::bit_cast<Refined<positive, int>>(-1) built a positive int that held
// -1.  The assignments are user-provided now, so the class is not
// trivially copyable and bit_cast refuses it at its constraint.

#include <fixy/Refined.h>

#include <bit>

int main() {
    auto forged = std::bit_cast<fixy::Refined<fixy::positive, int>>(-1);
    return forged.value();
}
