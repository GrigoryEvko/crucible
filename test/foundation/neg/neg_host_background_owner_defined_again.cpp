// A second definition of the background owner, which the background key
// names as a friend.  The first definition in a translation unit once
// built the key and minted a background context.  The owner is defined
// beside the key now, so this is a redefinition.

#include <foundation/effects/Effect.h>

namespace foundation::effects::host {
struct BackgroundOwner {
    static constexpr auto key() noexcept { return detail::ctx_mint::bg_key{}; }
};
}  // namespace foundation::effects::host

int main() { return 0; }
