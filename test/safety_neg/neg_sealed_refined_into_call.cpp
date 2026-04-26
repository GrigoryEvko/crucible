// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: SealedRefined<P, T> deletes the destructive `into() &&`
// rvalue extractor that Refined<P, T> exposes.  This is the LOAD-
// BEARING API difference between the two wrappers — SealedRefined
// forces every post-construction transformation through a fresh
// re-construction (which re-runs the predicate), eliminating the
// "extract → mutate behind the predicate's back → silently re-wrap"
// footgun.
//
// If `into()` ever silently returns to the SealedRefined surface,
// SealedRefined and Refined become indistinguishable, and the
// migration's load-bearing semantic distinction collapses.  The
// test catches that regression at compile time.

#include <crucible/safety/SealedRefined.h>

#include <utility>

constexpr bool positive_local(int x) noexcept { return x > 0; }

int main() {
    crucible::safety::SealedRefined<positive_local, int> sealed{42};
    // SealedRefined deliberately omits `into()`.  An rvalue-this
    // call must produce a "no member named 'into'" diagnostic.
    int extracted = std::move(sealed).into();
    return extracted;
}
