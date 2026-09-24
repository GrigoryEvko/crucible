// A test context, built over a buffer. A forged Test context admits
// production code to the gates of the test driver. No constructor is
// trivial, so the type is not an implicit-lifetime type, and the mandate
// of std::start_lifetime_as refuses it.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::Test>(storage);
    (void)forged;
    return 0;
}
