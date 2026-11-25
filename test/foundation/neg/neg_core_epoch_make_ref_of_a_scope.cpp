// make of an epoch builds an object that holds a Ref of the arena of a scope.
// The scope can end before the epoch, so the Ref would dangle.  make refuses a
// scope brand in an object of another arena (objects of an arena of
// foundation/core/BumpArena.h).

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
    Owner owner{};
    owner.scratch.with_epoch([](ScratchEpoch& epoch) noexcept {
        auto const alloc = ::foundation::effects::testing::test().alloc;
        ::foundation::core::with_bump_arena(
            alloc, ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096},
            [&epoch, alloc](auto& arena) noexcept {
                auto const value = arena.template make<int>(alloc, 3);
                auto const made = epoch.make<std::remove_const_t<decltype(value)>>(alloc, value);
                static_cast<void>(made);
            });
    });
    return 0;
}
