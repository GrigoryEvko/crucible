// A second definition of the foreground owner.  If foundation only
// declared the owner, the first definition in any translation unit would
// be legal C++, and its member would build the foreground key.  The owner
// is defined beside the key, so this is a redefinition.

#include <foundation/effects/Ctx.h>

namespace foundation::effects::host {
struct ForegroundOwner {
    static constexpr auto key() noexcept { return detail::ctx_mint::fg_key{}; }
};
}  // namespace foundation::effects::host

int main() { return 0; }
