// The end of an epoch ends each byte that the epoch gave out during the
// body, so a Ref of an object of the epoch must not leave it as the result.

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
    static_cast<void>(owner.scratch.with_epoch(
        [](::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>& epoch) noexcept {
            return epoch.make<int>(::foundation::effects::testing::test().alloc, 5);
        }));
    return 0;
}
