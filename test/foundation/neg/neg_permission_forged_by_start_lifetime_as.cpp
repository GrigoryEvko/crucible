// An exclusive token built over a buffer.  A class with a trivial
// constructor and a trivial destructor is an implicit-lifetime type, and
// std::start_lifetime_as starts the life of one over any storage, with
// no constructor call.  The move constructor of Permission is
// user-provided, so no constructor is trivial, and the mandate of
// std::start_lifetime_as refuses the type.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <bit>
#include <memory>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    alignas(fp::Permission<Region>) unsigned char storage[1]{};
    auto* forged = std::start_lifetime_as<fp::Permission<Region>>(storage);
    (void)forged;
    return 0;
}
