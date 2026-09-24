// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A brand names one borrow, and erasure runs one way only: a branded
// borrow converts to the erased spelling and to nothing else.  The two
// mints below give two brands.  A borrow of the second brand built from
// a borrow of the first would claim an identity the caller never
// minted.  The erased-brand check of the erasure constructor is what
// refuses it.
//
// Expected diagnostic: no constructor of the second branded Borrowed
// takes the first, and the note names the erased-brand check.

#include <fixy/Borrowed.h>

#include <utility>

namespace {
struct Owner {};
int storage[3] = {1, 2, 3};
}  // namespace

int main() {
    auto first = ::fixy::mint_borrowed<Owner>(storage);
    auto second = ::fixy::mint_borrowed<Owner>(storage);
    [[maybe_unused]] decltype(second) forged{first};
    return 0;
}
