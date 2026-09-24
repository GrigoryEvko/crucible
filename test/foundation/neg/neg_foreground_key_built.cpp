// The key of the producer claim is not built outside its two friends.
//
// The key is the evidence behind every foreground context.  Its default
// constructor is private, and only the owner of the producer claim and
// the test witness may call it.
//
// VIOLATION: a translation unit builds the key itself and mints a
// foreground context with it.
//
// Expected diagnostic: the key's constructor is private in this context.

#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    auto const forged = fe::mint_foreground_context(fe::detail::ctx_mint::fg_key{});
    static_cast<void>(forged);
    return 0;
}
