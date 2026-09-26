// A context narrows its row, and it never widens it.  The cold init
// context claims Init, Alloc and IO.  Block is an atom it does not claim,
// although its init source permits Block.
//
// Expected diagnostic: in_row has no candidate, because the startup load
// row is not a subrow of the cold init row.

#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::detail::ctx_witnesses::InitWitness const cold{fe::testing::init()};
    [[maybe_unused]] auto load =
        cold.in_row<fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>();
    return 0;
}
