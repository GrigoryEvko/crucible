// R003: coroutine x Row<Bg>.  The suspension and the background
// hand-off are two independent reasons the body outlives its caller, so
// a binding that names both says neither owns the lifetime.
//
// The pack trips R003 alone: L002 and R002 both need atom::borrow,
// which this pack does not name.  as_public is named because a Bg row
// on the strict Security pole is a classified value crossing into a
// background context, which the corpus refuses first.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::coroutine, ::fixy::atom::with_bg, ::fixy::atom::as_public> refused{};
    return 0;
}
