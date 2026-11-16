// The row of a context is the bound of the tag door, also where the door
// reads the source.  The observe context claims Bg and Nondet.  Its
// background source holds the field of IO, but a function handed the observe
// context does no IO.
//
// Expected diagnostic: extract_bare_from_ctx has no candidate, because the
// observe context does not own the IO capability.

#include <foundation/effects/Ctx.h>

namespace fe = ::foundation::effects;

// The row of the observe context of the background thread.
using ObserveCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Nondet>>;

int main() {
    ObserveCtx const observe{fe::testing::bg()};
    [[maybe_unused]] fe::cap::IO const io = fe::extract_bare_from_ctx<fe::Effect::IO>(observe);
    return 0;
}
