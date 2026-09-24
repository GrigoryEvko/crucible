// A caller that is not the producer claim asks the foreground owner for
// its key.  The member is private, and the claim is its one friend, so a
// foreground context comes only from a thread that won a claim.

#include <foundation/effects/Ctx.h>

int main() {
    const auto fg = ::foundation::effects::mint_foreground_context(::foundation::effects::host::ForegroundOwner::key());
    (void)fg;
    return 0;
}
