// A second definition of the init owner, with a door of its own.  The
// owner is defined in the header of the init key, so this is a
// redefinition.  No translation unit gets a door that the door guard
// cannot see.

#include <foundation/effects/Effect.h>

namespace foundation::effects::host {
struct InitOwner {
    static constexpr Init mint_init_context() noexcept { return mint_context<Init>(detail::ctx_mint::init_key{}); }
};
}  // namespace foundation::effects::host

int main() { return 0; }
