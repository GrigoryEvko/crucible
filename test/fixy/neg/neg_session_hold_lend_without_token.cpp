// A loan of a region the hold does not have.  lend is gated by the set,
// so a hold of Other cannot lend Region.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <functional>
#include <optional>
#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};
using TX = sess::Transferable<int, Region>;
}  // namespace

int main() {
    auto hold = sess::mint_permission_hold(fp::mint_permission_root<Other>());
    auto [loan, lent] = std::move(hold).lend<Region>(1);
    return loan.value;
}
