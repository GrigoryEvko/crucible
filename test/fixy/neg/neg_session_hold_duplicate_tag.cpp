// Two tokens of one tag in one hold.  A permission set holds each tag
// once, so the second token would be a second owner of the region.  The
// constraint on mint_permission_hold refuses the call.

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
    auto hold = sess::mint_permission_hold(fp::mint_permission_root<Region>(), fp::mint_permission_root<Region>());
    return hold.is_live() ? 0 : 1;
}
