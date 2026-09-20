// L002: borrow x async.  A borrowed reference's lifetime is tied to the
// caller's frame, and neither a suspension nor a hand-off to another
// thread keeps that frame alive, so the reference the binding carries
// may be read after the frame has unwound.
//
// The pack takes the Bg door: atom::borrow beside a row naming Bg.
// as_public is named because the strict Security pole is classified,
// and a classified value on a background row is what the corpus refuses
// first — without it this fixture would reject for the corpus's reason
// rather than the rule's.
//
// The pack also trips L007, whose premises (borrow x Row<Bg>) imply
// L002's (borrow x concurrent).  No pack isolates either from the
// other, so fn's tier-5 message lists both codes and this fixture
// stands on L002 appearing in it.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::borrow, ::fixy::atom::with_bg, ::fixy::atom::as_public> refused{};
    return 0;
}
