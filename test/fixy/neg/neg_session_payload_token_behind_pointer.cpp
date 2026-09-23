// A token laundered through a pair, behind a pointer.  The old walk read
// only the top of a payload, so a pair travelled with an empty set while
// the token stayed with the sender.  The walk reads every component and
// refuses a token that it reaches through a pointer.  The same pair with
// the token by value compiles, and moves the region.

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
    using Delta = sess::payload_perm_delta<std::pair<TX*, int>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
