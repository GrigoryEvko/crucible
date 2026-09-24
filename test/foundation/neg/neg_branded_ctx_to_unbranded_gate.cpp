// A context of a brand that the caller declared, handed to a gate that
// asks for the context with no brand.
//
// VIOLATION: any translation unit can declare a state and hold its claim,
// so a branded context that became the unbranded one would put every
// thread through the unbranded gate.
//
// Expected diagnostic: no conversion from the branded context.

#include <foundation/effects/Ctx.h>

namespace {
namespace fe = ::foundation::effects;

struct SelfDeclared {
    fe::host::ProducerClaim<SelfDeclared> claim;
};

void unbranded_gate(const fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>&) noexcept {}
}  // namespace

int main() {
    SelfDeclared state;
    unbranded_gate(state.claim.mint_producer_context());
    return 0;
}
