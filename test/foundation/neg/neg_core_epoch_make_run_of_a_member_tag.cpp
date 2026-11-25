// make of an epoch builds an object that holds a run of an arena of a member tag.
// The owner of that arena can end before the epoch, so the run would dangle.  The
// member tag of the epoch arena states the other tag, and an epoch has no tag, so
// the statement admits nothing in an epoch (objects of an arena of
// foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

using Bytes = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

struct Other {
    struct[[= ::foundation::brand::arena_member_brand{}]] Values final {};
    ::foundation::core::BumpArena<Values> values = ::foundation::core::mint_bump_arena<Values>(Bytes{4096});
};
struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]][[= ::foundation::brand::outlived_by<Other::Values>{}]]
        Scratch final {};
    ::foundation::core::EpochArena<Scratch> scratch = ::foundation::core::mint_epoch_arena<Scratch>(Bytes{4096});
};
using ScratchEpoch = ::foundation::core::Epoch<::foundation::core::EpochOf<Owner::Scratch>>;

namespace {
struct Holder {
    ::foundation::core::ArenaRun<int const, Other::Values> kept{};
};

void keep_a_run_of_a_member_tag(ScratchEpoch& epoch, ::foundation::core::ArenaRun<int const, Other::Values> values) {
    auto const made = epoch.make<Holder>(::foundation::effects::testing::test().alloc, Holder{values});
    static_cast<void>(made);
}
}  // namespace

int main() {
    static_cast<void>(&keep_a_run_of_a_member_tag);
    return 0;
}
