// The rvalue graded() moves the substrate out, and the substrate's own
// consume then hands out the payload.  It carries the payload constraint
// of extract, so a capability inside a pure-typed Computation does not
// leave through it.
//
// Expected diagnostic: graded() has no candidate, because the payload
// conveys an authority.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    using Pure = fe::Computation<fe::Row<>, fe::Capability<fe::Effect::IO, fe::Test>>;
    Pure pure = Pure::mint_computation(fe::mint_cap<fe::Effect::IO>(fe::testing::test()));
    [[maybe_unused]] auto substrate = std::move(pure).graded();
    return 0;
}
