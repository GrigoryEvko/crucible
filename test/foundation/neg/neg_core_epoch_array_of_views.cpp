// array of an epoch fills a run of Views.  A View names no owner, and the run
// lives until the epoch ends, so array refuses the element (objects of an
// arena of foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};
using ScratchEpoch = ::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>;

int main() {
    Owner owner{};
    return owner.scratch.with_epoch([](ScratchEpoch& epoch) noexcept -> int {
        auto const run =
            epoch.array(::foundation::effects::testing::test().alloc, 2, ::foundation::core::View<int const>{});
        return static_cast<int>(run.size());
    });
}
