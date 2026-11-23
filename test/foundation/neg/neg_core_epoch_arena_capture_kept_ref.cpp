// The body of an epoch writes a Ref of the epoch to an Option that it
// captures by reference.  The Ref outlives the epoch, so a read of it after
// the epoch reads ended bytes.  The gate of with_epoch refuses the capture at
// compile time.

#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};

int main() {
    Owner owner{};
    ::foundation::core::Option<::foundation::core::Ref<int, ::foundation::core::EpochOf<Owner::Scratch>>> kept{};
    owner.scratch.with_epoch(
        [&kept](::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>& epoch) noexcept {
            kept = ::foundation::core::some(epoch.make<int>(::foundation::effects::testing::test().alloc, 5));
        });
    return kept.is_some() ? 1 : 0;
}
