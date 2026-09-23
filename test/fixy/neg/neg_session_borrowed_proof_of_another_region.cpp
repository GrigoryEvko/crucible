// A Borrowed built from a read proof of a different region.  The proof
// of Other says nothing about Region, so the constructor has no match.

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
    auto other = fp::mint_permission_root<Other>();
    sess::Borrowed<int, Region> loan{1, fp::mint_read_view(other)};
    return loan.value;
}
