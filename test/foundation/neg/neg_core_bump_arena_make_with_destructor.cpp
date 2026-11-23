// The arena ends no object, so make refuses a type whose destructor does
// work: the destructor would never run.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

struct Ended {
    int value = 0;
    ~Ended() { value = 0; }
};

int main() {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    auto const object = arena.make<Ended>(::foundation::effects::testing::test().alloc);
    return object->value;
}
