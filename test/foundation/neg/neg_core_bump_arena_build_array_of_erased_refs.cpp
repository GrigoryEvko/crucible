// build_array of a BumpArena builds a run of erased Refs.  An erased Ref names
// no owner, and it can reach an object of a frame, so build_array refuses the
// element (objects of an arena of foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <type_traits>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    int value = 3;
    auto const erased = ::foundation::core::erase_brand(::foundation::core::to(value));
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    auto const run = arena.build_array<std::remove_const_t<decltype(erased)>>(
        ::foundation::effects::testing::test().alloc, ::foundation::core::ElementCount{2},
        [erased](::foundation::core::RunStart) noexcept { return erased; });
    return static_cast<int>(run.size());
}
