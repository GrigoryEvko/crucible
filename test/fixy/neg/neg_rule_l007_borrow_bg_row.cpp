// L007: borrow x Row<Bg>.  When the background thread runs the body,
// the caller's frame may already have unwound, and the borrow dangles.
// Move ownership into the closure, or drop the Bg atom.
//
// The pack is the one neg_rule_l002_borrow_async.cpp uses, and that is
// not an oversight: L007's premises are a special case of L002's, so
// every pack that trips L007 trips L002 too.  The two fixtures stand on
// different codes in the same message, which is what makes each rule's
// own refusal observable.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::borrow, ::fixy::atom::with_bg, ::fixy::atom::as_public> refused{};
    return 0;
}
