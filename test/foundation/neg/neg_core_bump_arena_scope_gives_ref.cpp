// The arena of with_bump_arena ends at the end of the scope, so a Ref of it
// must not leave the scope as the result of the body.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    static_cast<void>(::foundation::core::with_bump_arena(
        alloc, ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096},
        [alloc](auto& arena) noexcept { return arena.template make<int>(alloc, 5); }));
    return 0;
}
