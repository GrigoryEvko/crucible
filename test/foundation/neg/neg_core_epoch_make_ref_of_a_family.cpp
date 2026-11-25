// make of an epoch builds an object that holds a Ref of a family arena.  The body
// of the epoch can make that arena, and the arena then ends before the epoch, so
// the Ref would dangle.  An object of an epoch keeps only borrows of that epoch
// (objects of an arena of foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};
using ScratchEpoch = ::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>;
struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Holder {
    ::foundation::core::Ref<int, Family> kept;
};

void keep_a_ref_of_a_family(ScratchEpoch& epoch, ::foundation::core::Ref<int, Family> value) {
    auto const made = epoch.make<Holder>(::foundation::effects::testing::test().alloc, Holder{value});
    static_cast<void>(made);
}
}  // namespace

int main() {
    static_cast<void>(&keep_a_ref_of_a_family);
    return 0;
}
