// A claim of a brand that is a class local to a closure.  A claim keys its
// brand in the process-wide registry by a stable identity.  GCC prints a
// closure from its call signature, not from a declared name, so a class
// inside a closure has no stable identity.

#include <foundation/effects/Ctx.h>

int main() {
    [] {
        struct InClosure {
            ::foundation::effects::host::ProducerClaim<InClosure> claim;
        };
        InClosure state;
        (void)state.claim.mint_producer_context();
    }();
    return 0;
}
