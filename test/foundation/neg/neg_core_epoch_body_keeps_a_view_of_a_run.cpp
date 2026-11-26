// Negative-compile test. This file must fail to compile.
//
// The body of an epoch writes a View of a run of the epoch to a View that it
// captures by reference.  A View carries no brand, so the gate of the
// captures of the epoch admits it, but the View outlives the epoch, and a
// read of it after the epoch reads ended bytes.  The store gate of with_epoch
// (StoresNoEpochBorrowOutside of foundation/core/CaptureStore.h) refuses the
// body at compile time.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};

int main() {
    Owner owner{};
    ::foundation::core::View<std::uint64_t const> kept{};
    owner.scratch.with_epoch(
        [&kept](::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>& epoch) noexcept {
            auto const run = epoch.array<std::uint64_t>(::foundation::effects::testing::test().alloc, 1, 5);
            kept = run.view();
        });
    return static_cast<int>(kept.size());
}
