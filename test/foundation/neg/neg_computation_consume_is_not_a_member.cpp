// A Computation derives from its substrate privately.  The substrate's
// consume moves the payload out with no gate, so it is not a member of a
// Computation.  Through a public base, a pure-typed Computation handed
// out the IO capability that extract refuses.
//
// Expected diagnostic: the substrate's consume is inaccessible.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    using Pure = fe::Computation<fe::Row<>, fe::Capability<fe::Effect::IO, fe::Test>>;
    Pure pure = Pure::mint_computation(fe::mint_cap<fe::Effect::IO>(fe::testing::test()));
    [[maybe_unused]] auto laundered = std::move(pure).consume();
    return 0;
}
