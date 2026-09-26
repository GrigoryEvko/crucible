// A context narrows its row, and it never widens it.  The drain context
// claims Bg and Alloc.  IO is an atom it does not claim, and the evidence
// for IO is the background source itself, which the drain context keeps.
//
// Expected diagnostic: in_row has no candidate, because the compile row is
// not a subrow of the drain row.

#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::detail::ctx_witnesses::BgWitness const drain{fe::testing::bg()};
    [[maybe_unused]] auto compile = drain.in_row<fe::Row<fe::Effect::Bg, fe::Effect::Alloc, fe::Effect::IO>>();
    return 0;
}
