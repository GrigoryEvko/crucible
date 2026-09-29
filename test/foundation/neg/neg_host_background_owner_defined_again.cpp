// A second definition of the background owner, which the background key
// names as a friend.  A declaration alone beside the key would let the
// first definition in a translation unit build the key and mint a
// background context.  The owner is defined there, so this redefines it.

#include <foundation/effects/Effect.h>

namespace foundation::effects::host {
struct BackgroundOwner {
    static constexpr auto key() noexcept { return detail::ctx_mint::bg_key{}; }
};
}  // namespace foundation::effects::host

int main() { return 0; }
