// Corpus entry classified_bg_without_declassify.  Smith-Volpano 1998,
// Sabelfeld-Sands 2000, Hedin-Sabelfeld 2012: a classified value that
// crosses into a background-thread context makes that context's
// scheduling secret-dependent, and the spawn is itself a
// scheduler-observable event.  Sequential information-flow type systems
// are unsound under concurrency for exactly this reason.
//
// As with the IO entry, the pack is one atom: the strict Security pole
// is classified, so a Bg row alone is a classified value crossing into
// a background context.  role::BgWorker is this pack with as_public.
//
// No shipping declassification policy discharges the Bg channel, which
// is why the remediation names dropping the effect or projecting
// Security rather than adding a policy.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with_bg> refused{};
    return 0;
}
