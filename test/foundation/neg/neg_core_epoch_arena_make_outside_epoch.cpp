// An epoch arena gives make and array only inside an epoch, so an epoch
// always opens on an arena that holds no byte.  A make on the arena itself is
// refused: the arena has no such member.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};

int main() {
    Owner owner{};
    return *owner.scratch.make<int>(::foundation::effects::testing::test().alloc, 5);
}
