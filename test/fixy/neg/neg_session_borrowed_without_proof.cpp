// A Borrowed built with no read proof.  A read loan with no proof is a
// loan of a region the sender does not hold.  Borrowed has no constructor
// that takes the value alone.

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
    sess::Borrowed<int, Region> loan{1};
    return loan.value;
}
