// A borrow of the payload of a temporary marker dangles at the end of the
// full expression.  The rvalue overload of peek() is deleted.

#include <fixy/session/Flow.h>
#include <foundation/permissions/Permission.h>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    int const& dangling = sess::Transferable<int, Region>{1, fp::mint_permission_root<Region>()}.peek();
    return dangling;
}
