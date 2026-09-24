// A parked token built over a buffer would give the lender back a token
// that no loan parked.  The constructors of LentPermission are
// user-provided, so no constructor is trivial, the parked token is not an
// implicit-lifetime type, and the mandate of std::start_lifetime_as
// refuses it.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <memory>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    alignas(fp::LentPermission<Region>) unsigned char storage[1]{};
    auto* forged = std::start_lifetime_as<fp::LentPermission<Region>>(storage);
    (void)forged;
    return 0;
}
