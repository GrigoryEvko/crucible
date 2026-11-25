// A class states an arena tag as its brand_type, and its objects are local
// objects too.  to() would give a Ref of the tag for a local object, and an object
// of an arena of the tag could keep that Ref past the local object.  to() refuses a
// class that states the tag and that code outside the arena builds
// (carrier_brand_t of foundation/core/Ref.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Loan {
    using brand_type = Family;
    int value = 0;
};
}  // namespace

int main() {
    Loan loan{3};
    auto const ref = ::foundation::core::to(loan);
    return ref->value;
}
