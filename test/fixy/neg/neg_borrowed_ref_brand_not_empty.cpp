// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A brand names one borrow and carries no state.  A brand with a data
// member would give the borrow bytes that no mint wrote.  The type is
// spellable although no mint builds it, and the IsBrand check in the
// class body of BorrowedRef is what refuses the spelling.
//
// Expected diagnostic: the static_assert in BorrowedRef that asks for
// IsBrand<Brand>.

#include <fixy/Borrowed.h>

namespace {
struct StatefulBrand {
    int count = 0;
};
}  // namespace

int main() {
    return static_cast<int>(sizeof(::fixy::BorrowedRef<int, StatefulBrand>));
}
