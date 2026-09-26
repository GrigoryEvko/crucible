// The const graded() view reads the payload by reference.  A context is
// an authority by reference, because every ctx-bound gate takes its
// context by const reference, so the view carries the payload constraint
// of extract too.
//
// Expected diagnostic: graded() has no candidate, because the payload
// conveys an authority.

#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    using BgCtx = fe::detail::ctx_witnesses::BgWitness;
    auto const pure = fe::Computation<fe::Row<>, BgCtx>::mint_computation(BgCtx{fe::testing::bg()});
    [[maybe_unused]] auto const& view = pure.graded();
    return 0;
}
