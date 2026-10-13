// Exclusive tokens started over a buffer as an array.  The library
// function std::start_lifetime_as_array checks nothing about its element,
// so it gives a pointer to tokens that no mint made.  A token is an
// implicit-lifetime type, because its move constructor is trivial.  The
// checked start refuses a type that carries the annotation
// no_start_over_bytes, and the token carries it.

#include <foundation/Lifetime.h>
#include <foundation/permissions/Permission.h>

namespace fl = ::foundation::lifetime;
namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    alignas(fp::Permission<Region>) unsigned char storage[sizeof(fp::Permission<Region>)]{};
    auto forged = fl::start_as_array<fp::Permission<Region>>(storage, 1);
    (void)forged;
    return 0;
}
