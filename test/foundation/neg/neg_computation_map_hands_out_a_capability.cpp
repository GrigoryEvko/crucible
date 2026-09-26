// map passes the payload to its function.  A capability inside a
// pure-typed Computation would reach code that runs under the empty row,
// so map carries the payload constraint of extract.
//
// Expected diagnostic: map has no candidate, because the payload conveys
// an authority.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    using IoCap = fe::Capability<fe::Effect::IO, fe::Test>;
    using Pure = fe::Computation<fe::Row<>, IoCap>;
    Pure pure = Pure::mint_computation(fe::mint_cap<fe::Effect::IO>(fe::testing::test()));
    [[maybe_unused]] auto spent = std::move(pure).map([](IoCap token) {
        std::move(token).consume();
        return 0;
    });
    return 0;
}
