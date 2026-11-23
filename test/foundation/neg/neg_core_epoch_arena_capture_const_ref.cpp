// The body of an epoch captures a const Ref of the owner of the arena.  C++
// const is shallow: the Ref is const, and the owner that it names is not.
// Through it, the body could open a second epoch of the same arena.  The gate
// of with_epoch refuses the capture at compile time.

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
    auto const owner_ref = ::foundation::core::to(owner);
    return owner.scratch.with_epoch(
        [owner_ref](::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>& epoch) noexcept {
            static_cast<void>(owner_ref);
            return *epoch.make<int>(::foundation::effects::testing::test().alloc, 5);
        });
}
