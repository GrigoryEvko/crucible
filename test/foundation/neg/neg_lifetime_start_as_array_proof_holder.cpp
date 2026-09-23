// An aggregate that holds an exclusive token, started over a buffer as
// an array.  An aggregate is an implicit-lifetime type whatever its
// members are, and std::start_lifetime_as_array checks nothing about its
// element.  The member token would have no live object.  The checked
// start walks each subobject, finds the token, and refuses the type.

#include <foundation/Lifetime.h>
#include <foundation/permissions/Permission.h>

namespace fl = ::foundation::lifetime;
namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct HoldsToken {
    fp::Permission<Region> token;
};
}  // namespace

int main() {
    alignas(HoldsToken) unsigned char storage[sizeof(HoldsToken)]{};
    auto* forged = fl::start_as_array<HoldsToken>(storage, 1);
    (void)forged;
    return 0;
}
