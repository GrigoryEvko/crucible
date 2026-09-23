// S011: a capability x a replay-deterministic payload, at the PhiloxRng
// tier.
//
// PhiloxRng is the floor of the replay claim: a counter-based stream
// gives the same bits on every replay, so the claim is the same one Pure
// makes and the capability breaks it the same way.  This is the other
// mismatch class the rule reads, the boundary rather than the top of the
// chain, and the pack still trips S011 alone.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::PhiloxRng, int>,
                                ::fixy::atom::capability_usage, ::fixy::atom::trust_verified>
        refused{};
    return 0;
}
