// A token passed to the hold by name.  The hold takes each token by
// value, so the name would stay a second owner of a token that the hold
// now owns.  The copy of a Permission is deleted, so the call does not
// compile.  The same call with std::move compiles.

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
    auto token = fp::mint_permission_root<Region>();
    auto hold = sess::mint_permission_hold(token);
    return hold.is_live() ? 0 : 1;
}
