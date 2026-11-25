// build_array of an epoch builds a run of erased Refs.  An erased Ref names no
// owner, and it can reach an object of a frame, so build_array refuses the
// element (objects of an arena of foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <type_traits>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};
using ScratchEpoch = ::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>;

int main() {
    int value = 3;
    auto const erased = ::foundation::core::erase_brand(::foundation::core::to(value));
    Owner owner{};
    return owner.scratch.with_epoch([erased](ScratchEpoch& epoch) noexcept -> int {
        auto const run = epoch.build_array<std::remove_const_t<decltype(erased)>>(
            ::foundation::effects::testing::test().alloc, ::foundation::core::ElementCount{2},
            [erased](::foundation::core::RunStart) noexcept { return erased; });
        return static_cast<int>(run.size());
    });
}
