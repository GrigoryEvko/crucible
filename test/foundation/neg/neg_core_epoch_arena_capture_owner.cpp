// The body of an epoch captures the owner of the arena by a reference that is
// not const.  Through it, the body could open a second epoch of the same
// arena, whose end would end the bytes of the first epoch.  The gate of
// with_epoch refuses the capture at compile time.

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
    return owner.scratch.with_epoch(
        [&owner](::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>& epoch) noexcept {
            static_cast<void>(owner);
            return *epoch.make<int>(::foundation::effects::testing::test().alloc, 5);
        });
}
