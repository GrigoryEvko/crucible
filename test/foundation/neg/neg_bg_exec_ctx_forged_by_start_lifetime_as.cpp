// An execution context over Bg, built over a buffer. A forged execution
// context carries a Bg that no door minted, and cap() hands it out. No
// constructor is trivial, so the type is not an implicit-lifetime type,
// and the mandate of std::start_lifetime_as refuses it.

#include <foundation/effects/Ctx.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Block>>>(storage);
    (void)forged;
    return 0;
}
