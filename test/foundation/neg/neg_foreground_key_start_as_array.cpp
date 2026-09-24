// The key that mints a foreground context, started over a buffer and
// handed to the mint.  A forged key hands any thread the claim that it
// won the producer role.  No constructor of the key is trivial, so the
// key is not an implicit-lifetime type, and the checked lifetime start
// refuses it at its constraint.

#include <foundation/Lifetime.h>
#include <foundation/effects/Ctx.h>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto forged = fe::mint_foreground_context(
        ::foundation::lifetime::start_as_array<fe::detail::ctx_mint::fg_key>(storage, 1)[0]);
    (void)forged;
    return 0;
}
