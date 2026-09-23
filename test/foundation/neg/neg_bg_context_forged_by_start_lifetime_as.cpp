// A background context, built over a buffer. Every ctx-bound gate admits
// a scope that holds a Bg, so a forged one admits any scope. No
// constructor is trivial, so the type is not an implicit-lifetime type,
// and the mandate of std::start_lifetime_as refuses it.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::Bg>(storage);
    (void)forged;
    return 0;
}
