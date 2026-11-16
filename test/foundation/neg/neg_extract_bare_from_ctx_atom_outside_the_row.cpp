// The row of a context is the bound of the tag door, also where the door
// reads the source.  The observe context claims Bg and Nondet.  Its
// background source holds the field of IO, but a function handed the observe
// context does no IO.
//
// Expected diagnostic: extract_bare_from_ctx has no candidate, because the
// observe context does not own the IO capability.

#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::detail::ctx_witnesses::BgObserveCtx const observe{fe::testing::bg()};
    [[maybe_unused]] fe::cap::IO const io = fe::extract_bare_from_ctx<fe::Effect::IO>(observe);
    return 0;
}
