// make of a BumpArena builds an object that holds a Ref of an epoch.  The
// epoch ends before the arena, so the Ref would dangle.  make refuses an epoch
// brand in an object of another arena (objects of an arena of
// foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};
using ScratchEpoch = ::foundation::core::EpochOf<Owner::Scratch>;
struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Holder {
    using brand_type = ScratchEpoch;
    ::foundation::core::Ref<int, ScratchEpoch> kept;
};

void keep_a_ref_of_an_epoch(::foundation::core::BumpArena<Family>& arena,
                            ::foundation::core::Ref<int, ScratchEpoch> value) {
    auto const made = arena.make<Holder>(::foundation::effects::testing::test().alloc, Holder{value});
    static_cast<void>(made);
}
}  // namespace

int main() {
    static_cast<void>(&keep_a_ref_of_an_epoch);
    return 0;
}
