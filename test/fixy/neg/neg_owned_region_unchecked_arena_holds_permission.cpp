// The arena door of a region refuses an aggregate that holds a proof type,
// for each allocator.  Each aggregate is an implicit-lifetime type, so a
// test of the element alone admits it.  This allocator gives its bytes as T
// with no lifetime start, and it puts no constraint on T.  The door walks
// each subobject of the element, finds the Permission, and does not build
// the region.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

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

class UncheckedArena {
    alignas(64) unsigned char block_[256]{};

public:
    template <typename T>
    [[nodiscard]] T* alloc_array(::foundation::effects::Alloc, std::size_t) noexcept {
        return static_cast<T*>(static_cast<void*>(block_));
    }
};

}  // namespace

int main() {
    UncheckedArena arena{};
    auto region =
        ::fixy::mint_owned_region<HoldsProof>(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                              ::foundation::permissions::mint_permission_root<Cache>());
    auto token = std::move(region).release();
    (void)token;
    return 0;
}
