// A class outside the base states refers{} and a named arena tag as its
// second type argument.  Any code can write such a class, so its tag proves no
// owner, and make refuses it (objects of an arena of
// foundation/core/BumpArena.h).

#include <foundation/Transfer.h>
#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
template <class T, class Brand>
struct[[= ::foundation::transfer::refers{}]] OwnRef {
    using brand_type = Brand;
    T& target;
};

void keep_an_own_ref(::foundation::core::BumpArena<Family>& arena, int& value) {
    auto const made =
        arena.make<OwnRef<int, Family>>(::foundation::effects::testing::test().alloc, OwnRef<int, Family>{value});
    static_cast<void>(made);
}
}  // namespace

int main() {
    static_cast<void>(&keep_an_own_ref);
    return 0;
}
