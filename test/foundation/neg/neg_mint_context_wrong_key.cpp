// One passkey mints one context.  The factory's one constraint reads
// the context's own key_type, so a background key handed to the init
// context is refused at the call, and no translation unit can rebind a
// key by specializing anything.
//
// The key is never built here: forge::rvalue yields a reference to one
// without a constructor call, so the refusal is the constraint's and
// not the passkey's.
//
// VIOLATION: a TU mints an init context with a background key.
//
// Expected diagnostic: no matching call to mint_context, with the
// CanMintContext constraint unsatisfied.

#include <foundation/effects/Effect.h>

#include <cstdlib>

namespace forge {
template <class T>
[[gnu::noinline]] T&& rvalue() noexcept {
    std::abort();
}
}  // namespace forge

int main() {
    [[maybe_unused]] auto forged = ::foundation::effects::mint_context<::foundation::effects::Init>(
        forge::rvalue<::foundation::effects::detail::ctx_mint::bg_key>());
    return 0;
}
