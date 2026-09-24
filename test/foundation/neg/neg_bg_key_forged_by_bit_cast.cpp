// The key that mints a background context, built from a byte. A forged
// key opens the door of Bg from any scope. Each constructor is user-
// provided, so the type is not trivially copyable, and std::bit_cast has
// no candidate.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::detail::ctx_mint::bg_key>(char{0});
    (void)forged;
    return 0;
}
