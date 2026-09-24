// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The pointer-and-count constructor of Borrowed is a door for a C
// boundary.  A pointer carries no identity that a brand could name, so
// the door exists on the erased identity only.  Without its
// requires-clause, a caller could claim a brand of its own choosing for
// any range of memory.
//
// Expected diagnostic: no constructor of the branded Borrowed takes a
// pointer and a count, and the note names the erased-brand check.

#include <fixy/Borrowed.h>

#include <cstddef>

namespace {
struct Owner {};
struct ClaimedBrand {};
int storage[4] = {1, 2, 3, 4};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::Borrowed<int, Owner, ClaimedBrand> forged{storage, std::size_t{4}};
    return 0;
}
