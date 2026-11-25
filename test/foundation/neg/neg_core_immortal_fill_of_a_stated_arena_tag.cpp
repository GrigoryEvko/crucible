// An Immortal holds its object in static storage, and fill gives a Ref with the
// brand that the class states.  A class that states an arena tag and that code
// outside the arena builds is no object of an arena, so fill refuses it
// (carrier_brand_t of foundation/core/Ref.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Immortal.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Loan {
    using brand_type = Family;
    int value = 0;
};
}  // namespace

int main() {
    static ::foundation::core::Immortal<Loan> holder{};
    static_cast<void>(holder.fill(3));
    return 0;
}
