// A background context, built from a byte. Every ctx-bound gate admits a
// scope that holds a Bg, so a forged one admits any scope. Each
// constructor is user-provided, so the type is not trivially copyable,
// and std::bit_cast has no candidate.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::Bg>(char{0});
    (void)forged;
    return 0;
}
