// An object in a temporary arena dangles at the end of the statement, because
// the arena gives back its blocks there.  The rvalue form of make is deleted.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

int main() {
    auto const object = ::foundation::core::BumpArena{4096}.make<int>(::foundation::effects::Alloc{}, 5);
    return *object;
}
