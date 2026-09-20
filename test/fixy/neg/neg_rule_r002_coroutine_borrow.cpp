// R002: coroutine x borrow.  A coroutine resumes after the caller's
// frame may have unwound, which dangles the borrow.  This is L002's
// suspension half stated on its own axis pair: the Reentrancy grade
// rather than the effect row.
//
// The pack trips L002 as well, and cannot avoid it: L002's premise is
// borrow beside a coroutine OR a Bg row, so R002's premise implies it.
// The fixture stands on R002 appearing in the code list.
//
// No Security atom is needed here.  The pack names no effect row, so
// the corpus has no classified channel to refuse and the refusal is the
// rules' alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::borrow, ::fixy::atom::coroutine> refused{};
    return 0;
}
