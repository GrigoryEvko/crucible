// A token laundered through a pair, inside a std::optional.  The token
// can be absent at run time, and a set change cannot depend on a value,
// so the walk refuses it.

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
    using Delta = sess::payload_perm_delta<std::pair<std::optional<TX>, int>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
