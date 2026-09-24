// An execution context over Bg, built from a byte. A forged execution
// context carries a Bg that no door minted, and cap() hands it out. Each
// constructor is user-provided, so the type is not trivially copyable,
// and std::bit_cast has no candidate.

#include <foundation/effects/Ctx.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Block>>>(char{0});
    (void)forged;
    return 0;
}
