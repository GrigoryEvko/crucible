// Corpus entry internal_io_without_declassify.  Bell-LaPadula 1973,
// Volpano-Smith-Irvine 1996, Sabelfeld-Myers 2003: no-write-down holds
// for the Internal tier too.  Org-internal data sits below the strict
// classified pole but above public, and every non-public to public
// crossing needs an audit-trail discharge, not just the classified and
// secret tiers.
//
// atom::as_internal is written out because Internal is BELOW the strict
// pole: a binding reaches it only by naming it, which is the opposite of
// the classified entries, where saying nothing is what puts the value on
// the carrier.  That asymmetry is why this entry exists beside
// classified_io_without_declassify rather than being subsumed by it, and
// test/fixy/test_corpus.cpp holds the two apart in both directions.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::as_internal, ::fixy::atom::with_io> refused{};
    return 0;
}
