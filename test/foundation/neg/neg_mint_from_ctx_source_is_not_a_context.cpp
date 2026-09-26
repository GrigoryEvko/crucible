// mint_from_ctx reads the row of an execution context.  A capability
// source is not one: it has no row, and mint_cap is the mint that reads a
// source.
//
// Expected diagnostic: mint_from_ctx has no candidate, because the
// background source is not an execution context.

#include <foundation/effects/Capability.h>

int main() {
    namespace fe = ::foundation::effects;
    auto const source = fe::testing::bg();
    [[maybe_unused]] auto alloc = fe::mint_from_ctx<fe::Effect::Alloc>(source);
    return 0;
}
