// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// from_raw_nonnull builds a borrow from an address that arrived from C.
// An address carries no identity that a brand could name, so the factory
// exists on the erased identity only.  Without its requires-clause, a
// caller could claim a brand of its own choosing for any address.
//
// Expected diagnostic: from_raw_nonnull is not a member the branded
// BorrowedRef can name, and the note names the erased-brand check.

#include <fixy/Borrowed.h>

namespace {
struct ClaimedBrand {};
}  // namespace

int main() {
    int value = 3;
    [[maybe_unused]] auto forged = ::fixy::BorrowedRef<int, ClaimedBrand>::from_raw_nonnull(&value);
    return 0;
}
