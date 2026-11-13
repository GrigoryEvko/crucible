// The arena ends no object, so make refuses a type whose destructor does
// work: the destructor would never run.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

struct Ended {
    int value = 0;
    ~Ended() { value = 0; }
};

int main() {
    ::foundation::core::BumpArena arena{4096};
    auto const object = arena.make<Ended>(::foundation::effects::Alloc{});
    return object->value;
}
