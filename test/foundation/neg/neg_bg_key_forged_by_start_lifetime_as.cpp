// The key that mints a background context, built over a buffer. A forged
// key opens the door of Bg from any scope. No constructor is trivial, so
// the type is not an implicit-lifetime type, and the mandate of
// std::start_lifetime_as refuses it.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::detail::ctx_mint::bg_key>(storage);
    (void)forged;
    return 0;
}
