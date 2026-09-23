// A read proof built over a buffer.  The default constructor of ReadView
// is user-provided, so no constructor is trivial, the view is not an
// implicit-lifetime type, and the mandate of std::start_lifetime_as
// refuses it.

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
    alignas(fp::ReadView<Region>) unsigned char storage[1]{};
    auto* forged = std::start_lifetime_as<fp::ReadView<Region>>(storage);
    (void)forged;
    return 0;
}
