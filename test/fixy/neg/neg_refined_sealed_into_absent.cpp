// SealedRefined has no into().  The absence is the whole difference
// from Refined: every change to a sealed value goes through a fresh
// mint, which re-runs the predicate.  A caller who pattern-matches on
// the Refined idiom gets a name-lookup error, not a moved-out value.

#include <fixy/Refined.h>

#include <utility>

int main() {
    fixy::SealedRefined<fixy::positive, int> sealed = fixy::mint_sealed_refined<fixy::positive>(7);
    int extracted = std::move(sealed).into();
    return extracted;
}
