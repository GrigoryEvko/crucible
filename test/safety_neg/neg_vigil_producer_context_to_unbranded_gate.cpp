// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The context that a Vigil's claim mints names the Vigil.  A gate that
// asks for the context with no brand names no single-producer state, and
// any file can declare a brand of its own, so the Vigil's context does not
// become the unbranded context.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>

namespace {
void unbranded_gate(const ::fixy::HotFgCtx&) noexcept {}
}  // namespace

int main() {
    crucible::Vigil vigil;
    unbranded_gate(vigil.mint_producer_context());
    return 0;
}
