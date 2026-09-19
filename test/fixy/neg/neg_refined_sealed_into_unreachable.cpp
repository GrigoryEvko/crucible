// A sealed refinement has no extractor a caller can reach.  That is the
// whole difference from an ordinary one: every change to a sealed value
// goes through a fresh mint, which re-runs the predicate.
//
// The two refinements are one template now, so into() is declared once
// and constrained to the unsealed half.  A caller who pattern-matches on
// the Refined idiom therefore reads "the constraint !Sealed is false"
// rather than "no such member", and the name is unreachable either way.

#include <fixy/Refined.h>

#include <utility>

int main() {
    fixy::SealedRefined<fixy::positive, int> sealed = fixy::mint_sealed_refined<fixy::positive>(7);
    int extracted = std::move(sealed).into();
    return extracted;
}
