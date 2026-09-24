// The key that mints a foreground context, built from a byte and handed
// to the mint.  A forged key hands any thread the claim that it won the
// producer role.  Each constructor of the key is user-provided, so the
// type is not trivially copyable, and std::bit_cast has no candidate.

#include <foundation/effects/Ctx.h>

#include <bit>

namespace fe = ::foundation::effects;

int main() {
    auto forged = fe::mint_foreground_context(std::bit_cast<fe::detail::ctx_mint::fg_key>(char{0}));
    (void)forged;
    return 0;
}
