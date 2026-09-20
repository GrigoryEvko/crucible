// Corpus entry classified_io_without_declassify.  Sabelfeld-Myers 2003,
// after the Volpano-Smith-Irvine 1996 type-system foundation: a
// classified value that reaches an I/O boundary with no
// audit-discharging declassification policy is an implicit information
// flow out of the program.
//
// The pack is one atom, and that is the point.  Classified is the STRICT
// POLE of the Security axis, so a binding that says nothing about
// Security is classified, and naming an IO row alone puts a classified
// value on an observable channel.  Reject-by-default is what makes this
// the shortest refused pack in the corpus.
//
// The admitted spelling is the same pack with atom::as_public, which
// role::IoFunction is, or with a declassification naming the policy that
// licenses the export, which role::PublicEmit is.
//
// No collision rule reads this pair, so the refusal is the corpus's
// alone and the message carries the entry and its citation.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with_io> refused{};
    return 0;
}
