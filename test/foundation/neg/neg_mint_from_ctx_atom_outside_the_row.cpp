// The row of a context is the bound of mint_from_ctx.  The drain context
// claims Bg and Alloc.  Its background source permits IO too, but a
// function handed the drain context does no IO.
//
// Expected diagnostic: mint_from_ctx has no candidate, because the drain
// context does not own the IO capability.

#include <foundation/effects/Capability.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::detail::ctx_witnesses::BgWitness const drain{fe::testing::bg()};
    [[maybe_unused]] auto io = fe::mint_from_ctx<fe::Effect::IO>(drain);
    return 0;
}
