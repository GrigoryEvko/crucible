// A read loan built over a buffer would be a read right that no parked
// token waits for.  The default constructor and the move constructor of
// ReadLoan are user-provided, so no constructor is trivial, the loan is
// not an implicit-lifetime type, and the mandate of std::start_lifetime_as
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
    alignas(fp::ReadLoan<Region>) unsigned char storage[1]{};
    auto* forged = std::start_lifetime_as<fp::ReadLoan<Region>>(storage);
    (void)forged;
    return 0;
}
