// The arena door of a region refuses an aggregate that holds a proof type,
// for each allocator.  Each aggregate is an implicit-lifetime type, so a test
// of the element alone admits it.  UncheckedArena puts no constraint on its
// element.  The door walks each subobject of the element, finds the
// Permission, and does not build the region.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <utility>

namespace {

struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};
struct Proofs {
    using permission_row = ::foundation::effects::Row<>;
};

struct HoldsProof {
    ::foundation::permissions::Permission<Proofs> proof;
};

}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto region =
        ::fixy::mint_owned_region<HoldsProof>(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                              ::foundation::permissions::mint_permission_root<Cache>());
    auto token = std::move(region).release();
    (void)token;
    return 0;
}
