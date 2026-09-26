// A context lends its source only when its row claims every atom that
// the source permits.  The cold init context claims Init, Alloc and IO.
// Its init source permits Block too, so the cold init context keeps its
// source: with the source, a caller would build a context that claims
// Block.
//
// Expected diagnostic: cap() has no candidate, because the cold init row
// does not cover the init source.

#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::detail::ctx_witnesses::InitWitness const cold{fe::testing::init()};
    [[maybe_unused]] fe::Init const& source = cold.cap();
    return 0;
}
