// A translation unit writes a door of its own to the background context.
// The background key has a private constructor, and its friends are the
// background owner and the test witness.  So the forged door cannot
// build the key.

#include <foundation/effects/Effect.h>

namespace forge {
[[nodiscard]] constexpr ::foundation::effects::Bg mint_background_context() noexcept {
    return ::foundation::effects::mint_context<::foundation::effects::Bg>(
        ::foundation::effects::detail::ctx_mint::bg_key{});
}
}  // namespace forge

int main() {
    auto bg = forge::mint_background_context();
    (void)bg;
    return 0;
}
