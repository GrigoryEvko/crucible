// The annotation branded_arena marks an owner of the base, and to() gives an owner
// a fresh brand.  A class outside the base that states the annotation gets nothing
// from it: to() refuses the class as a carrier of the arena tag that other code
// builds (carrier_brand_t of foundation/core/Ref.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct[[= ::foundation::brand::branded_arena{}]] LookAlike {
    using brand_type = Family;
    int value = 0;
};
}  // namespace

int main() {
    LookAlike look_alike{3};
    auto const ref = ::foundation::core::to(look_alike);
    return ref->value;
}
