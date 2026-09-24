// A translation unit writes a door of its own to the init context.  The
// init key has a private constructor, and its friends are the init owner
// and the test witness.  So the forged door cannot build the key.

#include <foundation/effects/Effect.h>

namespace forge {
[[nodiscard]] constexpr ::foundation::effects::Init mint_init_context() noexcept {
    return ::foundation::effects::mint_context<::foundation::effects::Init>(
        ::foundation::effects::detail::ctx_mint::init_key{});
}
}  // namespace forge

int main() {
    auto init = forge::mint_init_context();
    (void)init;
    return 0;
}
