// A context lends its source only when its row claims every atom that
// the source permits.  The drain context claims Bg and Alloc, and its
// background source permits IO and Block too, so the drain context keeps
// its source.  With the source, mint_cap would mint IO.
//
// Expected diagnostic: cap() has no candidate, because the drain row does
// not cover the background source.

#include <foundation/effects/Capability.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::detail::ctx_witnesses::BgWitness const drain{fe::testing::bg()};
    [[maybe_unused]] auto io = fe::mint_cap<fe::Effect::IO>(drain.cap());
    return 0;
}
