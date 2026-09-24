// A payload behind type erasure.  The static type of a std::function
// does not name what it holds, so a token inside it would move with no
// set change.  The walk refuses the family by name.

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
    using Delta = sess::payload_perm_delta<std::pair<int, std::function<void()>>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
