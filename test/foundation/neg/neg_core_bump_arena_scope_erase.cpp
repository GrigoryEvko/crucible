// erase_brand refuses the scope brand of with_bump_arena, so a Ref of the
// arena of the scope gets no erased form that could outlive the scope.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::with_bump_arena(
        alloc, ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096},
        [alloc](auto& arena) noexcept {
            static_cast<void>(::foundation::core::erase_brand(arena.template make<int>(alloc, 5)));
        });
    return 0;
}
