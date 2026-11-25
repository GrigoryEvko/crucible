// make of a BumpArena builds an object that holds a Ref of an arena of a second
// family.  The tag of the arena states the tag of a third family and not the
// second one, so nothing tells that the second arena outlives the object
// (outlived_by of foundation/Brand.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Second final {};
struct[[= ::foundation::brand::arena_family_brand{}]] Third final {};
struct[[= ::foundation::brand::arena_family_brand{}]][[= ::foundation::brand::outlived_by<Third>{}]] First final {};

namespace {
struct Holder {
    ::foundation::core::Ref<int, Second> kept;
};

void keep_a_ref_of_another_family(::foundation::core::BumpArena<First>& arena,
                                  ::foundation::core::Ref<int, Second> value) {
    auto const made = arena.make<Holder>(::foundation::effects::testing::test().alloc, Holder{value});
    static_cast<void>(made);
}
}  // namespace

int main() {
    static_cast<void>(&keep_a_ref_of_another_family);
    return 0;
}
