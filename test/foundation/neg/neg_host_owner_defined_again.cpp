// A second definition of the foreground owner.  The owner was declared in
// foundation and defined nowhere, so the first definition in any
// translation unit was legal C++, and its member built the foreground key.
// The owner is defined beside the key now, so this is a redefinition.

#include <foundation/effects/Ctx.h>

namespace foundation::effects::host {
struct ForegroundOwner {
    static constexpr auto key() noexcept { return detail::ctx_mint::fg_key{}; }
};
}  // namespace foundation::effects::host

int main() { return 0; }
